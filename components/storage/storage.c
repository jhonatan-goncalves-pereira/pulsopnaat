/*
 * storage — implementação (ver storage.h).
 */
#include "storage.h"
#include "rtc_ds3231.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "storage";

/* RNF09: escrita em task dedicada, baixa prioridade (abaixo da amostragem e
 * do processamento, acima do idle) — nunca compete pelo core do DSP. */
#define TAREFA_STORAGE_PRIORIDADE 3
#define TAREFA_STORAGE_STACK 4096
#define TAREFA_STORAGE_CORE 0

/* Descartes só são logados a cada N ocorrências — evita floodar o console
 * quando o cartão está ausente e a task de processamento continua a
 * ~1 Hz indefinidamente. */
#define LOG_DESCARTE_A_CADA 60

static const char CSV_CABECALHO[] =
    "timestamp,node_id,estado_maquina,estado_equipamento,"
    "rms_x,h1x_x,h2x_x,b3x5_x,kurt_x,thd_x,"
    "rms_y,h1x_y,h2x_y,b3x5_y,kurt_y,thd_y,"
    "rms_z,h1x_z,h2x_z,b3x5_z,kurt_z,thd_z\n";

static QueueHandle_t s_fila;
static TaskHandle_t s_task;
static sdmmc_card_t *s_card;
static i2c_master_dev_handle_t s_rtc_dev;
static volatile bool s_sd_montado;
static volatile bool s_rtc_ok;
static FILE *s_arquivo;
static int64_t s_arquivo_aberto_us;
static uint32_t s_descartes_desde_log;

/* ------------------------------ nomes CSV -------------------------------- *
 * Tokens curtos, sem espaço/parênteses (diferente de mqtt_nome_estado_*):
 * mais simples de filtrar/colorir por valor no Grafana sem precisar de
 * regex ou aspas no parser CSV. */

static const char *nome_estado_maquina_csv(estado_maquina_t e)
{
    switch (e) {
    case ESTADO_MAQ_BOOT:         return "BOOT";
    case ESTADO_MAQ_CALIBRANDO:   return "CALIBRANDO";
    case ESTADO_MAQ_MONITORANDO:  return "MONITORANDO";
    case ESTADO_MAQ_CONTINGENCIA: return "CONTINGENCIA";
    default:                      return "?";
    }
}

static const char *nome_estado_equipamento_csv(estado_equipamento_t e)
{
    switch (e) {
    case ESTADO_EQUIP_VERDE:    return "VERDE";
    case ESTADO_EQUIP_AMARELO:  return "AMARELO";
    case ESTADO_EQUIP_VERMELHO: return "VERMELHO";
    default:                    return "?";
    }
}

/* --------------------------- montagem do SD ------------------------------ */

static esp_err_t montar_sd(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    /* Default do SDSPI tenta subir a até 20 MHz depois da negociação
     * inicial — jumpers longos/soltos em protoboard costumam não aguentar
     * isso, dando exatamente timeout no meio da negociação (0x107, visto em
     * campo nesta bancada). Teto mais conservador troca throughput (que não
     * importa pra 1 registro CSV por segundo) por robustez de sinal. */
    host.max_freq_khz = CONFIG_PULSOPNAAT_SD_SPI_MAX_FREQ_KHZ;

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_PULSOPNAAT_SD_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_PULSOPNAAT_SD_SPI_MISO_GPIO, /* silk "MOSO" no módulo */
        .sclk_io_num = CONFIG_PULSOPNAAT_SD_SPI_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg,
                                       SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi_bus_initialize falhou (%s) — microSD indisponível",
                 esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)CONFIG_PULSOPNAAT_SD_SPI_CS_GPIO;
    slot_cfg.host_id = (spi_host_device_t)host.slot;

    err = esp_vfs_fat_sdspi_mount(CONFIG_PULSOPNAAT_SD_MOUNT_POINT, &host, &slot_cfg,
                                   &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cartão microSD não montado (%s) — RNF09: sistema segue "
                      "sem log local, monitoramento/MQTT não são afetados",
                 esp_err_to_name(err));
        spi_bus_free((spi_host_device_t)host.slot);
        return err;
    }

    char dir[160];
    snprintf(dir, sizeof(dir), "%s/%s", CONFIG_PULSOPNAAT_SD_MOUNT_POINT,
             CONFIG_PULSOPNAAT_LOG_DIR);
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir(%s) falhou (%s)", dir, strerror(errno));
        /* Segue mesmo assim — fopen abaixo revelará se o diretório é usável. */
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "microSD montado em %s (SCK=%d MOSI=%d MISO=%d CS=%d)",
             CONFIG_PULSOPNAAT_SD_MOUNT_POINT, CONFIG_PULSOPNAAT_SD_SPI_SCK_GPIO,
             CONFIG_PULSOPNAAT_SD_SPI_MOSI_GPIO, CONFIG_PULSOPNAAT_SD_SPI_MISO_GPIO,
             CONFIG_PULSOPNAAT_SD_SPI_CS_GPIO);
    return ESP_OK;
}

/* ----------------------- rotação por tempo (RF12 + Grafana) -------------- */

/*
 * Poda de capacidade (rede de segurança, NÃO política de retenção padrão):
 * RF12 pede acúmulo persistente — isso continua sendo o padrão. Mas um
 * cartão real tem fim, e um SD cheio travaria toda escrita futura (RNF09).
 * Só entra em ação quando o espaço livre cai abaixo de
 * CONFIG_PULSOPNAAT_LOG_ESPACO_MINIMO_KB — nesse caso apaga o(s) log(s)
 * mais antigo(s) (nomes com timestamp do RTC ordenam cronologicamente por
 * string, "log_20260911_..." < "log_20260911_...") até voltar a ter folga.
 */
static void podar_logs_antigos_se_necessario(void)
{
    uint64_t total_bytes = 0, livre_bytes = 0;
    if (esp_vfs_fat_info(CONFIG_PULSOPNAAT_SD_MOUNT_POINT, &total_bytes, &livre_bytes) !=
        ESP_OK) {
        return; /* não deu pra consultar — não arrisca apagar às cegas */
    }

    const uint64_t limite_bytes = (uint64_t)CONFIG_PULSOPNAAT_LOG_ESPACO_MINIMO_KB * 1024ULL;
    if (livre_bytes >= limite_bytes) {
        return;
    }

    char dir[160];
    snprintf(dir, sizeof(dir), "%s/%s", CONFIG_PULSOPNAAT_SD_MOUNT_POINT,
             CONFIG_PULSOPNAAT_LOG_DIR);

    for (int tentativas = 0; tentativas < CONFIG_PULSOPNAAT_LOG_PODA_MAX_ARQUIVOS;
        ++tentativas) {
        DIR *d = opendir(dir);
        if (d == NULL) {
            ESP_LOGW(TAG, "poda: não foi possível abrir %s (%s)", dir, strerror(errno));
            return;
        }

        char mais_antigo[192] = {0};
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const size_t len = strlen(ent->d_name);
            /* Só considera os próprios arquivos de log ("log_*.csv") —
             * nunca mexe em nada que o usuário tenha posto no cartão. */
            if (strncmp(ent->d_name, "log_", 4) != 0 || len < 5 ||
                strcmp(ent->d_name + len - 4, ".csv") != 0) {
                continue;
            }
            if (mais_antigo[0] == '\0' || strcmp(ent->d_name, mais_antigo) < 0) {
                strncpy(mais_antigo, ent->d_name, sizeof(mais_antigo) - 1);
            }
        }
        closedir(d);

        if (mais_antigo[0] == '\0') {
            ESP_LOGW(TAG, "poda: espaço livre baixo mas não sobrou log antigo pra apagar");
            return;
        }

        char caminho[224];
        snprintf(caminho, sizeof(caminho), "%s/%s", dir, mais_antigo);
        if (remove(caminho) != 0) {
            ESP_LOGW(TAG, "poda: falha ao apagar %s (%s)", caminho, strerror(errno));
            return;
        }
        ESP_LOGW(TAG, "poda: espaço livre baixo (%llu KB < %d KB) — apagado log mais "
                      "antigo: %s",
                 (unsigned long long)(livre_bytes / 1024),
                 CONFIG_PULSOPNAAT_LOG_ESPACO_MINIMO_KB, mais_antigo);

        if (esp_vfs_fat_info(CONFIG_PULSOPNAAT_SD_MOUNT_POINT, &total_bytes, &livre_bytes) !=
            ESP_OK) {
            return;
        }
        if (livre_bytes >= limite_bytes) {
            return; /* já tem espaço de novo */
        }
    }

    ESP_LOGE(TAG, "poda: apagou %d arquivos numa única rotação e AINDA está com pouco "
                  "espaço — cartão perto do fim ou %d KB de limite alto demais pra ele",
             CONFIG_PULSOPNAAT_LOG_PODA_MAX_ARQUIVOS, CONFIG_PULSOPNAAT_LOG_ESPACO_MINIMO_KB);
}

static esp_err_t abrir_novo_arquivo(const struct tm *hora_opt)
{
    char caminho[192];
    if (hora_opt != NULL) {
        char carimbo[24];
        strftime(carimbo, sizeof(carimbo), "%Y%m%d_%H%M%S", hora_opt);
        snprintf(caminho, sizeof(caminho), "%s/%s/log_%s.csv",
                 CONFIG_PULSOPNAAT_SD_MOUNT_POINT, CONFIG_PULSOPNAAT_LOG_DIR, carimbo);
    } else {
        /* Sem RTC confiável: nome pelo uptime (ms) — monotônico dentro do
         * mesmo boot; evita colisão com arquivos de boots anteriores na
         * prática (cartão datado por RTC na maioria das sessões). */
        snprintf(caminho, sizeof(caminho), "%s/%s/log_boot_%lldms.csv",
                 CONFIG_PULSOPNAAT_SD_MOUNT_POINT, CONFIG_PULSOPNAAT_LOG_DIR,
                 (long long)(esp_timer_get_time() / 1000));
    }

    FILE *f = fopen(caminho, "a");
    if (f == NULL) {
        ESP_LOGW(TAG, "falha ao abrir %s (%s)", caminho, strerror(errno));
        return ESP_FAIL;
    }
    if (ftell(f) == 0) {
        /* Arquivo novo (ou vazio) — cabeçalho. Reabertura do mesmo nome
         * (raro: mesmo segundo de RTC) não duplica cabeçalho. */
        fputs(CSV_CABECALHO, f);
        fflush(f);
    }

    s_arquivo = f;
    s_arquivo_aberto_us = esp_timer_get_time();
    ESP_LOGI(TAG, "novo arquivo de log: %s (rotação a cada %d min)", caminho,
             CONFIG_PULSOPNAAT_LOG_ROTACAO_MIN);
    return ESP_OK;
}

static void rotacionar_se_necessario(const struct tm *hora_opt)
{
    const int64_t limite_us =
        (int64_t)CONFIG_PULSOPNAAT_LOG_ROTACAO_MIN * 60LL * 1000000LL;
    if (s_arquivo != NULL && (esp_timer_get_time() - s_arquivo_aberto_us) < limite_us) {
        return;
    }
    if (s_arquivo != NULL) {
        fclose(s_arquivo);
        s_arquivo = NULL;
    }
    podar_logs_antigos_se_necessario();
    (void)abrir_novo_arquivo(hora_opt); /* falha aqui → s_arquivo fica NULL,
                                          * write() abaixo descarta e conta */
}

/* -------------------------------- task ------------------------------------ */

static void tarefa_storage(void *arg)
{
    (void)arg;
    storage_registro_t reg;

    for (;;) {
        if (xQueueReceive(s_fila, &reg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!s_sd_montado) {
            if (++s_descartes_desde_log >= LOG_DESCARTE_A_CADA) {
                ESP_LOGW(TAG, "cartão indisponível — %u registros descartados",
                         (unsigned)s_descartes_desde_log);
                s_descartes_desde_log = 0;
            }
            continue;
        }

        struct tm agora;
        const bool tem_hora = s_rtc_ok && (ds3231_get_time(s_rtc_dev, &agora) == ESP_OK);
        if (s_rtc_ok && !tem_hora) {
            /* RTC estava OK mas a leitura falhou agora — não desliga
             * permanentemente, só usa fallback nesta linha. */
            ESP_LOGW(TAG, "leitura do DS3231 falhou — timestamp cai para boot+s nesta linha");
        }

        rotacionar_se_necessario(tem_hora ? &agora : NULL);
        if (s_arquivo == NULL) {
            if (++s_descartes_desde_log >= LOG_DESCARTE_A_CADA) {
                ESP_LOGW(TAG, "arquivo de log indisponível — %u registros descartados",
                         (unsigned)s_descartes_desde_log);
                s_descartes_desde_log = 0;
            }
            continue;
        }

        char ts_buf[32];
        if (tem_hora) {
            strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &agora);
        } else {
            snprintf(ts_buf, sizeof(ts_buf), "boot+%.3f", esp_timer_get_time() / 1e6);
        }

        const metricas_t *m = &reg.metricas;
        fprintf(s_arquivo,
                "%s,%s,%s,%s,"
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                ts_buf, CONFIG_PULSOPNAAT_NODE_ID,
                nome_estado_maquina_csv(reg.estado_maquina),
                nome_estado_equipamento_csv(reg.estado_equipamento),
                m->rms[EIXO_X], m->harmonica_1x[EIXO_X], m->harmonica_2x[EIXO_X],
                m->banda_3x_5x[EIXO_X], m->kurtosis[EIXO_X], m->thd[EIXO_X],
                m->rms[EIXO_Y], m->harmonica_1x[EIXO_Y], m->harmonica_2x[EIXO_Y],
                m->banda_3x_5x[EIXO_Y], m->kurtosis[EIXO_Y], m->thd[EIXO_Y],
                m->rms[EIXO_Z], m->harmonica_1x[EIXO_Z], m->harmonica_2x[EIXO_Z],
                m->banda_3x_5x[EIXO_Z], m->kurtosis[EIXO_Z], m->thd[EIXO_Z]);
        /* fflush por linha: ~1 escrita/s (janela de 1 s), custo aceitável na
         * task de baixa prioridade e garante que um reset/queda de energia
         * perca no máximo a última janela — não o arquivo inteiro (RF12:
         * "sobreviver a reinicializações"). */
        fflush(s_arquivo);
    }
}

/* ------------------------------- API pública ------------------------------ */

esp_err_t storage_init(i2c_master_bus_handle_t bus_handle)
{
    s_fila = xQueueCreate(CONFIG_PULSOPNAAT_LOG_FILA_PROFUNDIDADE,
                          sizeof(storage_registro_t));
    if (s_fila == NULL) {
        ESP_LOGE(TAG, "falha ao criar fila de log — storage desabilitado");
        return ESP_ERR_NO_MEM;
    }

    /* Cartão e RTC são independentes: falha em um não impede o outro nem
     * aborta o boot (degradação, não erro fatal — RF12/RNF09). */
    s_sd_montado = (montar_sd() == ESP_OK);

    if (bus_handle != NULL) {
        s_rtc_ok = (ds3231_init(bus_handle, &s_rtc_dev) == ESP_OK);
    } else {
        ESP_LOGW(TAG, "bus_handle I2C nulo — RTC não inicializado, "
                      "timestamp cairá para boot+s");
    }

    if (xTaskCreatePinnedToCore(tarefa_storage, "storage", TAREFA_STORAGE_STACK, NULL,
                                TAREFA_STORAGE_PRIORIDADE, &s_task,
                                TAREFA_STORAGE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task de storage");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool storage_disponivel(void)
{
    return s_sd_montado;
}

void storage_log_janela(const storage_registro_t *reg)
{
    if (reg == NULL || s_fila == NULL) {
        return;
    }
    if (xQueueSend(s_fila, reg, 0) != pdTRUE) {
        /* Fila cheia (task de escrita atrasada, ex.: wear-leveling do
         * cartão — RNF09 documenta latências de centenas de ms) — descarta
         * o mais novo, nunca bloqueia quem chamou. */
        if (++s_descartes_desde_log >= LOG_DESCARTE_A_CADA) {
            ESP_LOGW(TAG, "fila de log cheia — %u registros descartados",
                     (unsigned)s_descartes_desde_log);
            s_descartes_desde_log = 0;
        }
    }
}
