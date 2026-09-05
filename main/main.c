/*
 * app_main — montagem do sistema (PulsoPNAAT): cria a fila e as tasks com
 * pinning de núcleo e conecta os componentes.
 *
 * Pipeline de aquisição→janelamento→cadeia espectral completa:
 *
 *   [BNO085] --I2C--> [vibration_sensor] --Queue(janela_t)--> [processamento]
 *    ACCEL (m/s²)       task amostragem          500×3 floats     task core 1
 *    ~500 Hz            core 0, prio alta        janela de 1 s    prio média
 *                                                               │
 *                              analisar_janela(janela, f0, ·)   ▼
 *                       RMS + Hann + FFT-512 (esp-dsp) + harmônicos 1x/2x
 *                       + banda 3x–5x + kurtosis + THD, por eixo (X, Y, Z)
 *
 * A task de processamento consome cada janela, chama a função de análise
 * `analisar_janela` (signal_processing) com f0 = RPM_nominal/60 (Kconfig) e
 * imprime TODAS as métricas por eixo no serial em CSV, além de medir a
 * latência de processamento (RNF01: deve ficar abaixo da própria janela,
 * 1 s — variável crítica do projeto, requisitos §8). Nenhuma classificação
 * ainda nesta fase (ticket 03).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "i2c_config.h"
#include "signal_processing.h"
#include "vibration_sensor.h"

static const char *TAG = "app_main";

/* Processamento no core 1, prioridade média, isolado da amostragem (core 0)
 * para que o cálculo das métricas tenha timing determinístico.
 * Stack: 8 KB — o buffer de janela fica EM STATIC (4,8 KB não caberiam na
 * pilha; estouro canônico detectado on-device); o esp-dsp usa buffers
 * estáticos internos do componente, não a pilha. */
#define TAREFA_PROCESSAMENTO_CORE  1
#define TAREFA_PROCESSAMENTO_PRIORIDADE 5
#define TAREFA_PROCESSAMENTO_STACK 8192

/* Orçamento de processamento: a própria janela (1 s). RNF01. */
#define RNF01_LIMITE_US 1000000LL

static QueueHandle_t s_fila_janelas;

static void tarefa_processamento(void *arg)
{
    (void)arg;
    /* f0 = RPM_nominal/60 — configurado por equipamento no boot (Kconfig,
     * sem autodetecção); fixo durante a execução. */
    const float f0_hz = (float)CONFIG_PULSOPNAAT_RPM_NOMINAL / 60.0f;

    /* janela_t (~4,8 KB) em static: estouraria a pilha da task. Consumo é
     * single-task (esta), então não há disputa pelo buffer — e o mesmo
     * single-consumer torna os buffers de rascunho do signal_processing
     * seguros. */
    static janela_t janela;
    metricas_t metricas;

    /* Cabeçalho CSV: uma linha por janela, 6 métricas × 3 eixos (X, Y, Z). */
    printf("# rms_x,h1x_x,h2x_x,b3x5_x,kurt_x,thd_x,"
           "rms_y,h1x_y,h2x_y,b3x5_y,kurt_y,thd_y,"
           "rms_z,h1x_z,h2x_z,b3x5_z,kurt_z,thd_z\n");
    printf("# unidades: rms/h1x/h2x/b3x5 em m/s²; kurt e thd adimensionais;"
           " f0 = %.3f Hz (RPM nominal %d)\n",
           f0_hz, CONFIG_PULSOPNAAT_RPM_NOMINAL);

    for (;;) {
        if (xQueueReceive(s_fila_janelas, &janela, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* RNF01: medição da latência de processamento (entrada → saída). */
        const int64_t t0 = esp_timer_get_time();
        analisar_janela(&janela, f0_hz, &metricas);
        const int64_t latencia_us = esp_timer_get_time() - t0;

        printf("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
               "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
               "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
               metricas.rms[EIXO_X],
               metricas.harmonica_1x[EIXO_X], metricas.harmonica_2x[EIXO_X],
               metricas.banda_3x_5x[EIXO_X], metricas.kurtosis[EIXO_X],
               metricas.thd[EIXO_X],
               metricas.rms[EIXO_Y],
               metricas.harmonica_1x[EIXO_Y], metricas.harmonica_2x[EIXO_Y],
               metricas.banda_3x_5x[EIXO_Y], metricas.kurtosis[EIXO_Y],
               metricas.thd[EIXO_Y],
               metricas.rms[EIXO_Z],
               metricas.harmonica_1x[EIXO_Z], metricas.harmonica_2x[EIXO_Z],
               metricas.banda_3x_5x[EIXO_Z], metricas.kurtosis[EIXO_Z],
               metricas.thd[EIXO_Z]);

        if (latencia_us >= RNF01_LIMITE_US) {
            ESP_LOGW(TAG, "RNF01 VIOLADO: janela processada em %lld ms "
                          "(limite: %lld ms)", latencia_us / 1000LL,
                     (long long)(RNF01_LIMITE_US / 1000LL));
        } else {
            ESP_LOGI(TAG, "janela processada em %.1f ms (RNF01 < %lld ms)",
                     (double)latencia_us / 1000.0,
                     (long long)(RNF01_LIMITE_US / 1000LL));
        }
    }
}

void app_main(void)
{
    /* Instala o driver da UART de console com ring buffer de TX antes de
     * qualquer printf(). Sem isso, printf() usa busy-wait caractere a caractere
     * — a 115200 baud, uma linha grande pode girar a CPU tempo suficiente para
     * matar a task IDLE e derrubar o watchdog. Com o driver, as escritas são
     * interrompidas por IRQ e a task bloqueia/cede. */
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
                                        256, 1024, 0, NULL, 0));

    ESP_LOGI(TAG, "PulsoPNAAT: aquisição ACCEL (m/s²) 500 Hz + cadeia espectral "
                  "completa (RMS, FFT-512/Hann, harmônicos, kurtosis, THD)");

    /* Tabelas do esp-dsp para a FFT (idempotente; aborta no boot se falhar). */
    signal_processing_init();

    /* Fila de janelas: itens do tipo janela_t (~4,8 KB por cópia). Profundidade
     * curta de propósito — atraso acumulado indica consumo lento e a política
     * é descartar a janela mais antiga (vibration_sensor), nunca travar a
     * amostragem. */
    s_fila_janelas = xQueueCreate(CONFIG_PULSOPNAAT_FILA_JANELAS, sizeof(janela_t));
    if (s_fila_janelas == NULL) {
        ESP_LOGE(TAG, "falha ao criar fila de janelas");
        return;
    }

    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_dev_handle_t bno085_dev = NULL;
    ESP_ERROR_CHECK(i2c_config_init(&bus_handle, &bno085_dev));
    ESP_ERROR_CHECK(vibration_sensor_start(bno085_dev, s_fila_janelas));

    if (xTaskCreatePinnedToCore(tarefa_processamento, "processamento",
                                TAREFA_PROCESSAMENTO_STACK, NULL,
                                TAREFA_PROCESSAMENTO_PRIORIDADE, NULL,
                                TAREFA_PROCESSAMENTO_CORE) != pdPASS) {
        ESP_LOGE(TAG, "falha ao criar task de processamento");
        return;
    }

    ESP_LOGI(TAG, "pipeline ativa: BNO085 ACCELEROMETER (0x01, m/s²) @ %d µs → "
                  "janelas de %d amostras → %d métricas × 3 eixos no serial "
                  "(CSV), f0 = %d RPM/60",
             CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US, JANELA_N_AMOSTRAS,
             6, CONFIG_PULSOPNAAT_RPM_NOMINAL);
    vTaskDelete(NULL);
}
