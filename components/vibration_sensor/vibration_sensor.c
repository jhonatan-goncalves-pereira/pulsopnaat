/*
 * vibration_sensor — implementação (ver vibration_sensor.h).
 */
#include "vibration_sensor.h"

#include "bno085.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "signal_processing.h"

#include <string.h>

static const char *TAG = "vibration_sensor";

/* Amostragem no core 0, prioridade alta. Stack 8 KB: o caminho do callback
 * carrega unions grandes (sh2_SensorValue_t); high-water ~2,1 KB medido. */
#define TAREFA_AMOSTRAGEM_CORE      0
#define TAREFA_AMOSTRAGEM_PRIORIDADE 10
#define TAREFA_AMOSTRAGEM_STACK     8192

/* Rede de segurança: bombeia o serviço mesmo sem INT (período > 2 janelas de
 * tolerância). Com CONFIG_FREERTOS_HZ=100, cada tick vale 10 ms. */
#define TIMEOUT_SERVICO_MS 20

/* Faixa aceitável da taxa efetiva em torno dos 500 Hz nominais (±10%). */
#define TAXA_MIN_HZ 450.0f
#define TAXA_MAX_HZ 550.0f

#define BNO085_INT_GPIO ((gpio_num_t)CONFIG_APP_BNO085_INT_GPIO)
#define BNO085_RST_GPIO ((gpio_num_t)CONFIG_APP_BNO085_RST_GPIO)

/*
 * Reporte ACCELEROMETER (0x01): o SH-2 entrega valores JÁ em m/s², sem
 * constante de conversão no nosso lado (o RAW foi descartado por não ter
 * escala documentada — decisão em SPEC.md, Barramento e sensor). Risco
 * residual aceito: degraus de bias lentos quando o estimador recalibra,
 * fora da banda de vibração e absorvidos pelo baseline.
 */

static bno085_handle_t s_bno085 = NULL;
static QueueHandle_t s_fila_janelas = NULL;
static SemaphoreHandle_t s_sem_dados = NULL;

/* Janela em preenchimento — acessada somente pelo contexto da task de
 * amostragem (callback do driver + laço da task), sem necessidade de lock. */
static janela_t s_janela;
static uint32_t s_janelas_enviadas = 0;
static uint32_t s_janelas_descartadas = 0;
static bool s_janela_iniciada = false;

static void IRAM_ATTR isr_dado_pronto(void *arg)
{
    (void)arg;
    BaseType_t maior_prioridade_despertou = pdFALSE;
    if (s_sem_dados != NULL) {
        xSemaphoreGiveFromISR(s_sem_dados, &maior_prioridade_despertou);
        if (maior_prioridade_despertou) {
            portYIELD_FROM_ISR();
        }
    }
}

/* Callback do driver: despachado por bno085_service() no contexto da task de
 * amostragem. Rápido e sem bloqueio — só acumula. */
static void callback_amostra(bno085_handle_t handle,
                             const bno085_sensor_value_t *valor, void *ctx)
{
    (void)handle;
    (void)ctx;

    if (valor == NULL || valor->sensor_id != BNO085_SENSOR_ACCELEROMETER) {
        return;
    }
    if (s_janela.n_amostras >= JANELA_N_AMOSTRAS) {
        return; /* Janela cheia aguardando envio pelo laço da task. */
    }

    const float ax = valor->data.accelerometer.x;
    const float ay = valor->data.accelerometer.y;
    const float az = valor->data.accelerometer.z;

    /* Backstop físico RNF07: rejeita leituras impossíveis (|a| > 20 g ou
     * não-finitas) ANTES de entrar na janela. Glitches plausíveis (0,2–3 g)
     * não são pegos aqui — ficam para o Hampel. Rejeição = janela fica com
     * <500 amostras (raro). */
    if (!amostra_valida(ax) || !amostra_valida(ay) || !amostra_valida(az)) {
        return;
    }

    const uint32_t i = s_janela.n_amostras;
    s_janela.amostras[i][EIXO_X] = ax;
    s_janela.amostras[i][EIXO_Y] = ay;
    s_janela.amostras[i][EIXO_Z] = az;

    if (!s_janela_iniciada) {
        s_janela.ts_primeira_us = valor->timestamp_us;
        s_janela_iniciada = true;
    }
    s_janela.ts_ultima_us = valor->timestamp_us;
    s_janela.n_amostras = i + 1;
}

static void finalizar_e_enviar_janela(void)
{
    const uint32_t n = s_janela.n_amostras;

    /* Taxa efetiva por timestamps SH-2 (tempo do sensor). */
    const uint64_t dt_us = s_janela.ts_ultima_us - s_janela.ts_primeira_us;
    const float taxa_hz =
        (dt_us > 0) ? ((float)(n - 1) * 1e6f / (float)dt_us) : 0.0f;
    const float delta_ms = (n > 1 && dt_us > 0)
                               ? ((float)dt_us / 1000.0f / (float)(n - 1))
                               : 0.0f;

    if (xQueueSend(s_fila_janelas, &s_janela, 0) != pdTRUE) {
        /* Fila cheia: descarta a janela mais antiga — amostragem nunca
         * bloqueia mesmo com o consumo atrasado. */
        janela_t antiga;
        if (xQueueReceive(s_fila_janelas, &antiga, 0) == pdTRUE) {
            s_janelas_descartadas++;
        }
        if (xQueueSend(s_fila_janelas, &s_janela, 0) != pdTRUE) {
            ESP_LOGE(TAG, "fila de janelas indisponível; janela perdida");
        }
    }
    s_janelas_enviadas++;

    const bool taxa_ok =
        (taxa_hz >= TAXA_MIN_HZ) && (taxa_hz <= TAXA_MAX_HZ) && (n == JANELA_N_AMOSTRAS);
    const esp_log_level_t nivel = taxa_ok ? ESP_LOG_INFO : ESP_LOG_WARN;
    ESP_LOG_LEVEL(nivel, TAG,
                  "janela #%lu: %lu amostras, Δ médio=%.2f ms, taxa efetiva=%.1f Hz%s"
                  " (enviadas=%lu descartadas=%lu)",
                  (unsigned long)s_janelas_enviadas, (unsigned long)n,
                  delta_ms, taxa_hz,
                  taxa_ok ? "" : " [FORA DA FAIXA ~500 Hz]",
                  (unsigned long)s_janelas_enviadas,
                  (unsigned long)s_janelas_descartadas);

    memset(&s_janela, 0, sizeof(s_janela));
    s_janela_iniciada = false;
}

static void tarefa_amostragem(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task de amostragem no core %d (prioridade %d), alvo %d Hz",
             TAREFA_AMOSTRAGEM_CORE, TAREFA_AMOSTRAGEM_PRIORIDADE,
             1000000 / CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US);

    for (;;) {
        /* Bloqueia até o INT do BNO085 indicar reporte novo; timeout apenas
         * garante progresso se o INT parar (ex.: sensor reiniciado). */
        xSemaphoreTake(s_sem_dados, pdMS_TO_TICKS(TIMEOUT_SERVICO_MS));
        bno085_service(s_bno085);

        if (s_janela.n_amostras >= JANELA_N_AMOSTRAS) {
            finalizar_e_enviar_janela();
        }
    }
}

esp_err_t vibration_sensor_start(i2c_master_dev_handle_t bno085_i2c_dev,
                                 QueueHandle_t fila_janelas)
{
    if (bno085_i2c_dev == NULL || fila_janelas == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_fila_janelas = fila_janelas;
    memset(&s_janela, 0, sizeof(s_janela));

    s_sem_dados = xSemaphoreCreateBinary();
    if (s_sem_dados == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Pino INT (H_INTN, ativo baixo): borda de descida sinaliza reporte novo. */
    const gpio_config_t cfg_int = {
        .pin_bit_mask = (1ULL << BNO085_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg_int), TAG, "falha ao configurar GPIO INT");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG,
                        "falha ao instalar serviço de ISR");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BNO085_INT_GPIO, isr_dado_pronto, NULL),
                        TAG, "falha ao registrar ISR do INT");

    bno085_config_t cfg = {0};
    bno085_config_default(&cfg);
    ESP_RETURN_ON_ERROR(bno085_init(&cfg, bno085_i2c_dev, BNO085_INT_GPIO,
                                    BNO085_RST_GPIO, &s_bno085),
                        TAG, "falha ao inicializar BNO085");

    ESP_RETURN_ON_ERROR(bno085_register_sensor_callback(s_bno085, callback_amostra, NULL),
                        TAG, "falha ao registrar callback");
    ESP_RETURN_ON_ERROR(bno085_enable_sensor(s_bno085, BNO085_SENSOR_ACCELEROMETER,
                                             CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US),
                        TAG, "falha ao habilitar ACCELEROMETER");

    const BaseType_t rc = xTaskCreatePinnedToCore(
        tarefa_amostragem, "amostragem", TAREFA_AMOSTRAGEM_STACK, NULL,
        TAREFA_AMOSTRAGEM_PRIORIDADE, NULL, TAREFA_AMOSTRAGEM_CORE);
    return (rc == pdPASS) ? ESP_OK : ESP_FAIL;
}
