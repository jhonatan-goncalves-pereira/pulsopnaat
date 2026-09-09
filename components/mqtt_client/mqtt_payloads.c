/*
 * mqtt_payloads — implementação pura (ver header para contrato).
 *
 * Só <stdio.h> + tipos puros (signal_processing/alert_manager). Sem ESP-IDF.
 */
#include "mqtt_payloads.h"

#include <stdio.h>

const char *mqtt_nome_estado_maquina(estado_maquina_t e)
{
    switch (e) {
    case ESTADO_MAQ_BOOT: return "BOOT";
    case ESTADO_MAQ_CALIBRANDO: return "CALIBRANDO";
    case ESTADO_MAQ_MONITORANDO: return "MONITORANDO";
    case ESTADO_MAQ_CONTINGENCIA: return "CONTINGENCIA";
    default: return "?";
    }
}

const char *mqtt_nome_estado_equipamento(estado_equipamento_t e)
{
    switch (e) {
    case ESTADO_EQUIP_VERDE: return "verde (normal)";
    case ESTADO_EQUIP_AMARELO: return "amarelo (atenção)";
    case ESTADO_EQUIP_VERMELHO: return "vermelho (crítico)";
    default: return "?";
    }
}

size_t mqtt_formatar_alerta(char *out, size_t out_len, const char *node_id,
                            int64_t ts_us, estado_equipamento_t estado,
                            const metricas_t *metricas)
{
    if (out == NULL || out_len == 0 || node_id == NULL || metricas == NULL) {
        return 0;
    }
    const int n = snprintf(out, out_len,
                           "{\"node\":\"%s\",\"ts_us\":%lld,\"estado\":\"%s\","
                           "\"rms_x\":%.4f,\"rms_y\":%.4f,\"rms_z\":%.4f}",
                           node_id, (long long)ts_us,
                           mqtt_nome_estado_equipamento(estado),
                           metricas->rms[EIXO_X], metricas->rms[EIXO_Y],
                           metricas->rms[EIXO_Z]);
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

size_t mqtt_formatar_status(char *out, size_t out_len, const char *node_id,
                            estado_maquina_t maquina,
                            estado_equipamento_t equipamento,
                            bool baseline_presente, int64_t uptime_s)
{
    if (out == NULL || out_len == 0 || node_id == NULL) {
        return 0;
    }
    const int n = snprintf(out, out_len,
                           "{\"node\":\"%s\",\"maquina\":\"%s\",\"equipamento\":\"%s\","
                           "\"baseline\":%s,\"uptime_s\":%lld}",
                           node_id, mqtt_nome_estado_maquina(maquina),
                           mqtt_nome_estado_equipamento(equipamento),
                           baseline_presente ? "true" : "false",
                           (long long)uptime_s);
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}
