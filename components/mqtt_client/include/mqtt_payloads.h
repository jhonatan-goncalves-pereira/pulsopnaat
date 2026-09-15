/*
 * mqtt_payloads — formatação JSON dos payloads MQTT (PulsoPNAAT).
 *
 * Módulo PURO: sem ESP-IDF, FreeRTOS, I/O ou estado global — os mesmos
 * fontes compilam no alvo e no corpus Unity on-target. Recebe os dados já
 * extraídos (estado, métricas, timestamps) e devolve o JSON em buffer do
 * chamador; transporte (mqtt_client_publish) e máquina de estados
 * (alerta_servico) ficam fora daqui.
 *
 * Nota RF05: o payload completo normativo (todas as 6 métricas, eixo(s) de
 * maior desvio) ainda é extensão; este formato é um superconjunto do que o
 * firmware publica hoje (estado + RMS por eixo) acrescido de node e
 * timestamp, que faltavam.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alert_manager.h"
#include "signal_processing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mapeamento estado → string do protocolo (mesmo vocabulário do log serial:
 * máquina "BOOT"/"CALIBRANDO"/"MONITORANDO"/"CONTINGENCIA"/"?"; equipamento
 * "verde (normal)"/"amarelo (atenção)"/"vermelho (crítico)"/"?"). Puro. */
const char *mqtt_nome_estado_maquina(estado_maquina_t e);
const char *mqtt_nome_estado_equipamento(estado_equipamento_t e);

/*
 * Formata o alerta de classificação:
 *   {"node":"...","ts_us":<i64>,"estado":"...","rms_x":%.4f,"rms_y":%.4f,"rms_z":%.4f}
 *
 * `metricas` NULL, `out`/`node_id` NULL ou `out_len` == 0 → retorna 0.
 * Truncação (saída não caberia com NUL) → retorna 0 e `out[0]` = '\0'.
 * Sucesso → nº de bytes escritos sem o NUL (> 0).
 */
size_t mqtt_formatar_alerta(char *out, size_t out_len, const char *node_id,
                            int64_t ts_us, estado_equipamento_t estado,
                            const metricas_t *metricas);

/*
 * Formata o status do nó:
 *   {"node":"...","maquina":"...","equipamento":"...","baseline":true|false,"uptime_s":<i64>}
 *
 * Mesmo contrato de retorno do alerta (`out`/`node_id` NULL ou `out_len`
 * == 0 → 0; truncação → 0 com `out[0]` = '\0').
 */
size_t mqtt_formatar_status(char *out, size_t out_len, const char *node_id,
                            estado_maquina_t maquina,
                            estado_equipamento_t equipamento,
                            bool baseline_presente, int64_t uptime_s);

/*
 * Telemetria por janela (consumida pelo Telegraf → InfluxDB → Grafana):
 *   {"node":"...","ts_us":<i64>,"maquina":"...","equipamento":"...","nivel":0|1|2|-1,
 *    "rms_x":..,"h1x_x":..,"h2x_x":..,"b3x5_x":..,"kurt_x":..,"thd_x":.., (idem y, z),
 *    "score":<f>|null,"anomalia":0|1,"rotulo":"..."}
 * `nivel` é o estado do equipamento em número (verde 0, amarelo 1, vermelho 2) para colorir
 * no Grafana; `score` não finito sai como null; `rotulo` NULL vira "sem_rotulo".
 * Mesmo contrato de retorno do alerta.
 */
size_t mqtt_formatar_telemetria(char *out, size_t out_len, const char *node_id, int64_t ts_us,
                                estado_maquina_t maquina, estado_equipamento_t equipamento,
                                const metricas_t *metricas, float score, bool anomalia,
                                const char *rotulo);

#ifdef __cplusplus
}
#endif
