/*
 * app_main — montagem do sistema (PulsoPNAAT): cria a fila e as tasks com
 * pinning de núcleo e conecta os componentes.
 *
 * Pipeline de aquisição→janelamento→RMS:
 *
 *   [BNO085] --I2C--> [vibration_sensor] --Queue(janela_t)--> [processamento]
 *    RAW_ACCEL          task amostragem          400×3 floats     task core 1
 *    ~400 Hz            core 0, prio alta        janela de 1 s    prio média
 *
 * A task de processamento consome cada janela, chama a função de análise
 * `analisar_janela` (signal_processing) e imprime o RMS por eixo no serial em
 * CSV. Nenhuma outra métrica, nenhuma classificação ainda nesta fase.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "i2c_config.h"
#include "signal_processing.h"
#include "vibration_sensor.h"

static const char *TAG = "app_main";

/* Processamento no core 1, prioridade média, isolado da amostragem (core 0)
 * para que o cálculo das métricas tenha timing determinístico. */
#define TAREFA_PROCESSAMENTO_CORE  1
#define TAREFA_PROCESSAMENTO_PRIORIDADE 5
#define TAREFA_PROCESSAMENTO_STACK 4096

static QueueHandle_t s_fila_janelas;

static void tarefa_processamento(void *arg)
{
    (void)arg;
    janela_t janela;
    metricas_t metricas;

    /* Cabeçalho CSV: uma linha por janela, RMS por eixo em m/s². */
    printf("# rms_x_ms2,rms_y_ms2,rms_z_ms2\n");

    for (;;) {
        if (xQueueReceive(s_fila_janelas, &janela, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        analisar_janela(&janela, &metricas);

        printf("%.4f,%.4f,%.4f\n",
               metricas.rms[EIXO_X], metricas.rms[EIXO_Y], metricas.rms[EIXO_Z]);
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

    ESP_LOGI(TAG, "PulsoPNAAT: aquisição RAW 400 Hz + janelamento + RMS");

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

    ESP_LOGI(TAG, "pipeline ativa: BNO085 RAW_ACCEL @ %d µs → janelas de %d amostras "
                  "→ RMS X,Y,Z no serial (CSV)",
             CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US, JANELA_N_AMOSTRAS);
    vTaskDelete(NULL);
}
