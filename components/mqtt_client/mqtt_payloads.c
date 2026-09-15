/*
 * mqtt_payloads — implementação pura (ver header para contrato).
 *
 * Só <stdio.h> + tipos puros (signal_processing/alert_manager). Sem ESP-IDF.
 */
#include "mqtt_payloads.h"

#include <math.h>
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

static int nivel_equipamento(estado_equipamento_t e)
{
    switch (e) {
    case ESTADO_EQUIP_VERDE: return 0;
    case ESTADO_EQUIP_AMARELO: return 1;
    case ESTADO_EQUIP_VERMELHO: return 2;
    default: return -1;
    }
}

size_t mqtt_formatar_telemetria(char *out, size_t out_len, const char *node_id, int64_t ts_us,
                                estado_maquina_t maquina, estado_equipamento_t equipamento,
                                const metricas_t *metricas, float score, bool anomalia,
                                const char *rotulo)
{
    if (out == NULL || out_len == 0 || node_id == NULL || metricas == NULL) {
        return 0;
    }
    char score_txt[48];
    if (isfinite(score)) {
        snprintf(score_txt, sizeof(score_txt), "%.4f", score);
    } else {
        snprintf(score_txt, sizeof(score_txt), "null");
    }
    const metricas_t *m = metricas;
    const int n = snprintf(out, out_len,
                           "{\"node\":\"%s\",\"ts_us\":%lld,\"maquina\":\"%s\",\"equipamento\":\"%s\",\"nivel\":%d,"
                           "\"rms_x\":%.4f,\"h1x_x\":%.4f,\"h2x_x\":%.4f,\"b3x5_x\":%.4f,\"kurt_x\":%.4f,\"thd_x\":%.4f,"
                           "\"rms_y\":%.4f,\"h1x_y\":%.4f,\"h2x_y\":%.4f,\"b3x5_y\":%.4f,\"kurt_y\":%.4f,\"thd_y\":%.4f,"
                           "\"rms_z\":%.4f,\"h1x_z\":%.4f,\"h2x_z\":%.4f,\"b3x5_z\":%.4f,\"kurt_z\":%.4f,\"thd_z\":%.4f,"
                           "\"score\":%s,\"anomalia\":%d,\"rotulo\":\"%s\"}",
                           node_id, (long long)ts_us, mqtt_nome_estado_maquina(maquina),
                           mqtt_nome_estado_equipamento(equipamento), nivel_equipamento(equipamento),
                           m->rms[EIXO_X], m->harmonica_1x[EIXO_X], m->harmonica_2x[EIXO_X],
                           m->banda_3x_5x[EIXO_X], m->kurtosis[EIXO_X], m->thd[EIXO_X],
                           m->rms[EIXO_Y], m->harmonica_1x[EIXO_Y], m->harmonica_2x[EIXO_Y],
                           m->banda_3x_5x[EIXO_Y], m->kurtosis[EIXO_Y], m->thd[EIXO_Y],
                           m->rms[EIXO_Z], m->harmonica_1x[EIXO_Z], m->harmonica_2x[EIXO_Z],
                           m->banda_3x_5x[EIXO_Z], m->kurtosis[EIXO_Z], m->thd[EIXO_Z],
                           score_txt, anomalia ? 1 : 0, rotulo != NULL ? rotulo : "sem_rotulo");
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}
