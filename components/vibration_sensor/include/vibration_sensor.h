/*
 * vibration_sensor — aquisição BNO085 RAW_ACCELEROMETER, janelamento e
 * buffers por eixo (PulsoPNAAT).
 *
 * Arquitetura:
 *   - Task de amostragem: core 0, prioridade alta. Bloqueia no semáforo do pino
 *     INT do BNO085 (H_INTN, ativo baixo) e bombeia bno085_service() a cada
 *     reporte. O polling por delay não é viável a ~400 Hz (reporte a cada
 *     2,5 ms > tick de 10 ms com CONFIG_FREERTOS_HZ=100); o timeout do
 *     semáforo é apenas rede de segurança caso o INT pare de chegar.
 *   - O callback do driver (despachado por bno085_service() DENTRO da task de
 *     amostragem) apenas acumula amostras no buffer da janela — sem locks:
 *     callback e task rodam no mesmo contexto de execução.
 *   - Ao completar N=400 amostras por eixo (janela de 1 s), a janela é enviada
 *     por cópia via Queue para a task de processamento (core 1). Fila cheia →
 *     descarta a janela mais antiga, para que a amostragem nunca seja
 *     bloqueada nem atrasada pelo consumo.
 *   - A taxa efetiva é medida pelos timestamps SH-2 dos reportes (tempo do
 *     sensor, imune a batching do SHTP) e registrada no log por janela,
 *     confirmando empiricamente o Δ ≈ 2,5 ms entre amostras.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicializa o BNO085 (reset SH-2, callback, RAW_ACCELEROMETER no intervalo de
 * CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US µs), instala o ISR do pino INT e
 * cria a task de amostragem (core 0, prioridade alta).
 *
 * `fila_janelas`: fila criada por app_main com itens do tipo `janela_t`
 * (signal_processing.h). A propriedade é de quem chama; este componente só
 * produz.
 *
 * Não é thread-safe com bno085_init/deinit em outras tasks (limitação do
 * driver SH-2): chamar uma única vez, a partir de app_main.
 */
esp_err_t vibration_sensor_start(i2c_master_dev_handle_t bno085_i2c_dev,
                                 QueueHandle_t fila_janelas);

#ifdef __cplusplus
}
#endif
