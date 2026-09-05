/*
 * vibration_sensor — aquisição BNO085 LINEAR_ACCELERATION (0x04, m/s² sem
 * gravidade), janelamento e buffers por eixo (PulsoPNAAT).
 *
 * Task de amostragem (core 0, prioridade alta) bloqueia no semáforo do pino
 * INT do BNO085 (H_INTN, ativo baixo) e bombeia bno085_service() a cada
 * reporte — polling por delay não é viável a ~400 Hz (reporte a cada 2,5 ms
 * > tick de 10 ms); o timeout é só rede de segurança. O callback do driver
 * roda no MESMO contexto (dentro de bno085_service) e apenas acumula — sem
 * locks. Janela completa (N=400, 1 s) vai por cópia via Queue para o core 1;
 * fila cheia → descarta a mais antiga, nunca bloqueia a amostragem.
 *
 * Reporte LINEAR_ACCELERATION (0x04), não RAW (0x14): valores já em m/s² pelo
 * SH-2 (sem constante de conversão não validada) e com a gravidade removida
 * por fusão — sem ela, o RMS de √(média(x²)) fica dominado pelo DC de
 * ~9,81 m/s² e quase não enxerga a vibração (revisão da decisão anterior em
 * requisitos.md v2.2; justificativa completa em SPEC.md, Barramento e
 * sensor). Taxa efetiva medida pelos timestamps SH-2 (tempo do sensor);
 * máx. do 0x04 = 400 Hz (datasheet).
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
 * Inicializa o BNO085 (reset SH-2, callback, LINEAR_ACCELERATION no intervalo
 * de CONFIG_PULSOPNAAT_SENSOR_REPORT_INTERVAL_US µs), instala o ISR do pino
 * INT e cria a task de amostragem (core 0, prioridade alta).
 *
 * `fila_janelas`: fila do chamador com itens `janela_t`; este componente só
 * produz. Não é thread-safe com bno085_init/deinit (limitação do driver
 * SH-2): chamar uma única vez, a partir de app_main.
 */
esp_err_t vibration_sensor_start(i2c_master_dev_handle_t bno085_i2c_dev,
                                 QueueHandle_t fila_janelas);

#ifdef __cplusplus
}
#endif
