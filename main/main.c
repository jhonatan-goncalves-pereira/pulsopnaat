/*
 * app_main — montagem do sistema (PulsoPNAAT): filas, tasks com pinning e
 * conexão dos componentes (tickets 01–04).
 *
 * Pipeline: BNO085 → vibration_sensor (amostragem, core 0) → fila janela_t →
 * processamento (core 1): Hampel + analisar_janela (RMS, Hann, FFT-512,
 * harmônicos, kurtosis, THD) → baseline (30 janelas, Welford) ou classificação
 * 3σ/6σ (votação por eixo, pior eixo) → alerta_servico (LED/buzzer) + MQTT.
 *
 * Calibração SEMPRE comandada (botão/serial/MQTT — RF08), nunca automática no
 * boot; o nó só entra em MONITORANDO com baseline válido.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h> /* strcasecmp — comandos serial insensíveis a caixa */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "alert_manager.h"
#include "alerta_servico.h"
#include "baseline.h"
#include "baseline_nvs.h"
#include "i2c_config.h"
#include "signal_processing.h"
#include "vibration_sensor.h"
#include "wifi_config.h"
#include "mqtt_client.h"

static const char *TAG = "app_main";

/* Core 1 isolado da amostragem (core 0) para timing determinístico.
 * Stack 8 KB: a janela fica em static (~4,8 KB não cabem na pilha — estouro
 * medido on-device); os buffers do esp-dsp são internos ao componente. */
#define TAREFA_PROCESSAMENTO_CORE 1
#define TAREFA_PROCESSAMENTO_PRIORIDADE 5
#define TAREFA_PROCESSAMENTO_STACK 8192

/* Task de comandos: botão (polling/debounce) + serial — core 0, prioridade
 * baixa (acima do idle, abaixo da amostragem). */
#define TAREFA_COMANDOS_CORE 0
#define TAREFA_COMANDOS_PRIORIDADE 2
#define TAREFA_COMANDOS_STACK 4096
#define COMANDOS_TICK_MS 20

/* Orçamento de processamento: a própria janela (1 s). RNF01. */
#define RNF01_LIMITE_US 1000000LL

/* Conectividade roda em contexto de aplicação (core 1, prioridade baixa). */
#define TAREFA_CONECTIVIDADE_CORE 1
#define TAREFA_CONECTIVIDADE_PRIORIDADE 2
#define TAREFA_CONECTIVIDADE_STACK 4096

static QueueHandle_t s_fila_janelas;

/* Baseline vigente + acumulador da calibração — só a task de processamento
 * toca (single-consumer, como o buffer de janela). */
static baseline_t s_baseline;
static baseline_calibracao_t s_calibracao;
static bool s_baseline_presente;

/* ------------------------- auxiliares de log ---------------------------- */

static const char *nome_estado_maquina(estado_maquina_t e)
{
    switch (e) {
    case ESTADO_MAQ_BOOT: return "BOOT";
    case ESTADO_MAQ_CALIBRANDO: return "CALIBRANDO";
    case ESTADO_MAQ_MONITORANDO: return "MONITORANDO";
    case ESTADO_MAQ_CONTINGENCIA: return "CONTINGENCIA";
    default: return "?";
    }
}

static const char *nome_estado_equipamento(estado_equipamento_t e)
{
    switch (e) {
    case ESTADO_EQUIP_VERDE: return "verde (normal)";
    case ESTADO_EQUIP_AMARELO: return "amarelo (atenção)";
    case ESTADO_EQUIP_VERMELHO: return "vermelho (crítico)";
    default: return "?";
    }
}

/* --------------------------- task de comandos ---------------------------- */

static void processar_linha(const char *linha)
{
    if (linha[0] == '\0') return;
    
    if (strcasecmp(linha, "calibrar") == 0) {
        ESP_LOGI(TAG, "comando serial: calibrar (máquina em %s)", nome_estado_maquina(alerta_servico_estado_maquina()));
        alerta_servico_publicar_evento(EVENTO_INICIAR_CALIBRACAO);
        return;
    }
    if (strcasecmp(linha, "status") == 0) {
        printf("máquina=%s equipamento=%s baseline=%s uptime=%lld s\n",
               nome_estado_maquina(alerta_servico_estado_maquina()),
               nome_estado_equipamento(alerta_servico_estado_equipamento()),
               s_baseline_presente ? "presente" : "ausente",
               (long long)(esp_timer_get_time() / 1000000LL));
        if (s_baseline_presente) {
            printf("baseline RMS (média±σ): X=%.4f±%.4f Y=%.4f±%.4f Z=%.4f±%.4f m/s²\n",
                   s_baseline.media[METRICA_RMS][EIXO_X], s_baseline.desvio_padrao[METRICA_RMS][EIXO_X],
                   s_baseline.media[METRICA_RMS][EIXO_Y], s_baseline.desvio_padrao[METRICA_RMS][EIXO_Y],
                   s_baseline.media[METRICA_RMS][EIXO_Z], s_baseline.desvio_padrao[METRICA_RMS][EIXO_Z]);
        }
        return;
    }
    printf("comandos: calibrar | status\n");
}

/* Conectividade (item 7): o callback WiFi roda na task do loop de eventos —
 * não pode bloquear nem chamar MQTT direto. Ele só sinaliza; esta task
 * (contexto de aplicação) inicia o MQTT e publica os eventos da máquina. */
static TaskHandle_t s_conectividade_task = NULL;
static volatile bool s_wifi_tem_ip = false;
static bool s_mqtt_iniciado = false;

static void tarefa_conectividade(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_wifi_tem_ip && !s_mqtt_iniciado) {
            ESP_LOGI(TAG, "WiFi com IP — iniciando cliente MQTT (task de aplicação)...");
            if (mqtt_client_start() == ESP_OK) {
                s_mqtt_iniciado = true;
            } else {
                ESP_LOGW(TAG, "mqtt_client_start falhou — tenta de novo no próximo GOT_IP");
            }
        }
    }
}

static void wifi_state_changed(wifi_state_t state)
{
    /* Contexto: loop de eventos ESP. Apenas ops não-bloqueantes aqui. */
    if (state == WIFI_STATE_GOT_IP) {
        s_wifi_tem_ip = true;
        alerta_servico_publicar_evento(EVENTO_WIFI_RESTAURADO);
        if (s_conectividade_task != NULL) {
            xTaskNotifyGive(s_conectividade_task);
        }
    } else if (state == WIFI_STATE_DISCONNECTED) {
        s_wifi_tem_ip = false;
        ESP_LOGW(TAG, "WiFi desconectado. Tentando reconectar...");
        alerta_servico_publicar_evento(EVENTO_WIFI_CAIR);
    } else if (state == WIFI_STATE_FAILED) {
        s_wifi_tem_ip = false;
        ESP_LOGE(TAG, "WiFi falhou (MAX_RETRY) — máquina em CONTINGÊNCIA até novo wifi_config_start()");
        alerta_servico_publicar_evento(EVENTO_WIFI_CAIR);
    }
}

static void mqtt_state_changed(mqtt_client_state_t state)
{
    if (state == MQTT_CLIENT_STATE_SUBSCRIBED) {
        ESP_LOGI(TAG, "MQTT conectado e inscrito nos tópicos. Sistema online.");
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "{\"status\":\"online\",\"baseline\":\"%s\"}", s_baseline_presente ? "presente" : "ausente");
        mqtt_client_publish(MQTT_TOPIC_STATUS, status_msg, 0, 1);
    }
}

static void mqtt_data_received(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "Comando MQTT recebido em %s: %.*s", topic, data_len, data);
    
    if (strncasecmp(data, "calibrar", data_len) == 0) {
        ESP_LOGI(TAG, "Acionando calibração via MQTT");
        alerta_servico_publicar_evento(EVENTO_INICIAR_CALIBRACAO);
    } else if (strncasecmp(data, "status", data_len) == 0) {
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "{\"maquina\":\"%s\",\"equipamento\":\"%s\",\"baseline\":%s}",
                 nome_estado_maquina(alerta_servico_estado_maquina()),
                 nome_estado_equipamento(alerta_servico_estado_equipamento()),
                 s_baseline_presente ? "true" : "false");
        mqtt_client_publish(MQTT_TOPIC_STATUS, status_msg, 0, 1);
    }
}

static void tarefa_comandos(void *arg)
{
    (void)arg;
    const gpio_num_t botao = (gpio_num_t)CONFIG_PULSOPNAAT_BOTAO_CALIBRACAO_GPIO;
    gpio_config_t cfg_botao = {
        .pin_bit_mask = 1ULL << botao,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_botao));

    char linha[32];
    size_t len = 0;
    uint32_t estaveis_baixo = 0, estaveis_alto = 0;
    bool pronto_para_apertar = true;

    for (;;) {
        if (gpio_get_level(botao) == 0) {
            estaveis_baixo++; estaveis_alto = 0;
        } else {
            estaveis_alto++; estaveis_baixo = 0;
        }
        if (pronto_para_apertar && estaveis_baixo >= 2) {
            pronto_para_apertar = false;
            ESP_LOGI(TAG, "botão de calibração pressionado (GPIO %d)", botao);
            alerta_servico_publicar_evento(EVENTO_INICIAR_CALIBRACAO);
        }
        if (!pronto_para_apertar && estaveis_alto >= 2) {
            pronto_para_apertar = true;
        }

        uint8_t ch;
        while (uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, 0) == 1) {
            if (ch == '\n' || ch == '\r') {
                if (len >= sizeof(linha)) len = sizeof(linha) - 1;
                linha[len] = '\0';
                processar_linha(linha);
                len = 0;
            } else if (len < sizeof(linha) - 1) {
                linha[len++] = (char)ch;
            } else {
                len = sizeof(linha);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(COMANDOS_TICK_MS));
    }
}

/* ------------------------- task de processamento ------------------------- */

static void tarefa_processamento(void *arg)
{
    (void)arg;
    const float f0_hz = (float)CONFIG_PULSOPNAAT_RPM_NOMINAL / 60.0f;
    static janela_t janela;
    metricas_t metricas;
    estado_maquina_t estado_visto = ESTADO_MAQ_BOOT;
    bool avisou_sem_baseline = false;

    printf("# rms_x,h1x_x,h2x_x,b3x5_x,kurt_x,thd_x,rms_y,h1x_y,h2x_y,b3x5_y,kurt_y,thd_y,rms_z,h1x_z,h2x_z,b3x5_z,kurt_z,thd_z\n");
    printf("# unidades: rms/h1x/h2x/b3x5 em m/s²; kurt e thd adimensionais; f0 = %.3f Hz (RPM nominal %d)\n", f0_hz, CONFIG_PULSOPNAAT_RPM_NOMINAL);

    for (;;) {
        if (xQueueReceive(s_fila_janelas, &janela, portMAX_DELAY) != pdTRUE) continue;

        const int64_t t0 = esp_timer_get_time();
        (void)janela_hampel(&janela, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
        analisar_janela(&janela, f0_hz, &metricas);
        const int64_t latencia_us = esp_timer_get_time() - t0;

        printf("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
               metricas.rms[EIXO_X], metricas.harmonica_1x[EIXO_X], metricas.harmonica_2x[EIXO_X], metricas.banda_3x_5x[EIXO_X], metricas.kurtosis[EIXO_X], metricas.thd[EIXO_X],
               metricas.rms[EIXO_Y], metricas.harmonica_1x[EIXO_Y], metricas.harmonica_2x[EIXO_Y], metricas.banda_3x_5x[EIXO_Y], metricas.kurtosis[EIXO_Y], metricas.thd[EIXO_Y],
               metricas.rms[EIXO_Z], metricas.harmonica_1x[EIXO_Z], metricas.harmonica_2x[EIXO_Z], metricas.banda_3x_5x[EIXO_Z], metricas.kurtosis[EIXO_Z], metricas.thd[EIXO_Z]);

        if (latencia_us >= RNF01_LIMITE_US) {
            ESP_LOGW(TAG, "RNF01 VIOLADO: janela processada em %lld ms (limite: %lld ms)", latencia_us / 1000LL, (long long)(RNF01_LIMITE_US / 1000LL));
        }

        const estado_maquina_t estado = alerta_servico_estado_maquina();
        const bool entrou_calibrando = (estado == ESTADO_MAQ_CALIBRANDO) && (estado_visto != estado);
        estado_visto = estado;

        switch (estado) {
        case ESTADO_MAQ_CALIBRANDO: {
            if (entrou_calibrando) {
                baseline_calibracao_iniciar(&s_calibracao);
                ESP_LOGI(TAG, "calibração iniciada: mantenha o regime saudável por %d janelas (~%d s)", BASELINE_N_JANELAS, BASELINE_N_JANELAS);
            }
            if (baseline_calibracao_adicionar(&s_calibracao, &metricas)) {
                baseline_t novo;
                if (baseline_calibracao_extrair(&s_calibracao, &novo) && baseline_valido(&novo)) {
                    s_baseline = novo;
                    s_baseline_presente = true;
                    const esp_err_t err = baseline_salvar_nvs(&s_baseline);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "calibração OK, mas não persistiu na NVS: %s", esp_err_to_name(err));
                    } else {
                        ESP_LOGI(TAG, "baseline calibrado (%d janelas) e salvo na NVS", BASELINE_N_JANELAS);
                    }
                    alerta_servico_publicar_evento(EVENTO_BASELINE_DISPONIVEL);
                } else {
                    ESP_LOGE(TAG, "métricas inválidas na calibração — coleta reiniciada");
                    baseline_calibracao_iniciar(&s_calibracao);
                }
            } else {
                ESP_LOGI(TAG, "calibração: janela %u/%u", s_calibracao.n, (uint32_t)BASELINE_N_JANELAS);
            }
            break;
        }
        case ESTADO_MAQ_MONITORANDO:
        case ESTADO_MAQ_CONTINGENCIA: {
            const estado_equipamento_t novo = alerta_classificar_janela(&s_baseline, &metricas);
            const estado_equipamento_t anterior = alerta_servico_estado_equipamento();
            const estado_equipamento_t efetivo = alerta_servico_definir_estado_equipamento(novo);
            
            if (efetivo != anterior) {
                ESP_LOGI(TAG, "estado do equipamento: %s → %s", nome_estado_equipamento(anterior), nome_estado_equipamento(efetivo));
                
                if (efetivo == ESTADO_EQUIP_AMARELO || efetivo == ESTADO_EQUIP_VERMELHO) {
                    char alert_msg[160];
                    snprintf(alert_msg, sizeof(alert_msg), "{\"estado\":\"%s\",\"rms_x\":%.4f,\"rms_y\":%.4f,\"rms_z\":%.4f}",
                             nome_estado_equipamento(efetivo), metricas.rms[EIXO_X], metricas.rms[EIXO_Y], metricas.rms[EIXO_Z]);
                    mqtt_client_publish(MQTT_TOPIC_ALERT, alert_msg, 0, 1);
                }
            }
            break;
        }
        case ESTADO_MAQ_BOOT:
        default:
            if (!avisou_sem_baseline) {
                avisou_sem_baseline = true;
                ESP_LOGW(TAG, "sem baseline: janelas descartadas até a calibração");
            }
            break;
        }
    }
}

/* --------------------------------- app_main ------------------------------ */

void app_main(void)
{
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 1024, 0, NULL, 0));
    ESP_LOGI(TAG, "PulsoPNAAT: aquisição 400 Hz + cadeia espectral + baseline/classificação + WiFi/MQTT (tickets 01–04)");
    signal_processing_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Inicializando WiFi e MQTT...");
    ESP_ERROR_CHECK(wifi_config_init());
    wifi_config_register_callback(wifi_state_changed);
    ESP_ERROR_CHECK(mqtt_client_init());
    mqtt_client_register_state_callback(mqtt_state_changed);
    mqtt_client_register_data_callback(mqtt_data_received);
    if (xTaskCreatePinnedToCore(tarefa_conectividade, "conectividade",
                                TAREFA_CONECTIVIDADE_STACK, NULL,
                                TAREFA_CONECTIVIDADE_PRIORIDADE,
                                &s_conectividade_task,
                                TAREFA_CONECTIVIDADE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task de conectividade");
        return;
    }
    ESP_ERROR_CHECK(wifi_config_start());

    ESP_ERROR_CHECK(alerta_servico_iniciar());

    err = baseline_carregar_nvs(&s_baseline);
    if (err == ESP_OK) {
        s_baseline_presente = true;
        ESP_LOGI(TAG, "baseline válido carregado da NVS");
        alerta_servico_publicar_evento(EVENTO_BASELINE_DISPONIVEL);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "sem baseline na NVS — aguardando comando de calibração");
    } else {
        ESP_LOGW(TAG, "baseline na NVS ilegível (%s)", esp_err_to_name(err));
    }

    s_fila_janelas = xQueueCreate(CONFIG_PULSOPNAAT_FILA_JANELAS, sizeof(janela_t));
    if (s_fila_janelas == NULL) {
        ESP_LOGE(TAG, "falha ao criar fila de janelas");
        return;
    }

    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t bno085_dev = NULL;
    ESP_ERROR_CHECK(i2c_config_init(&bus_handle, &bno085_dev));
    ESP_ERROR_CHECK(vibration_sensor_start(bno085_dev, s_fila_janelas));

    if (xTaskCreatePinnedToCore(tarefa_processamento, "processamento", TAREFA_PROCESSAMENTO_STACK, NULL, TAREFA_PROCESSAMENTO_PRIORIDADE, NULL, TAREFA_PROCESSAMENTO_CORE) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task de processamento");
        return; 
    }

    if (xTaskCreatePinnedToCore(tarefa_comandos, "comandos", TAREFA_COMANDOS_STACK, NULL, TAREFA_COMANDOS_PRIORIDADE, NULL, TAREFA_COMANDOS_CORE) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task de comandos");
        return;
    }

    ESP_LOGI(TAG, "Pipeline ativa e conectividade configurada. Aguardando IP...");
    vTaskDelete(NULL);
}