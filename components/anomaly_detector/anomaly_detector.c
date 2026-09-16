/*
 * anomaly_detector — inferência do detector de anomalia (ver anomaly_detector.h).
 *
 * Por janela: 18 métricas → transformação log (mesmas constantes do treino em
 * tools/classificador/treinar_detector.py) → padronização pela média/escala do
 * modelo → distância de Mahalanobis √(zᵀ·P·z) com a matriz de precisão gerada
 * em modelo_anomalia.h. Sem modelo treinado, anomalia_modelo_embarcado()
 * devolve NULL e o score sai NaN (detector desligado).
 */
#include "anomaly_detector.h"

#include <math.h>
#include <stddef.h>

#include "modelo_anomalia.h"

static float log_amplitude(float v)
{
    return logf((v > 0.0f ? v : 0.0f) + ANOMALIA_EPS_AMPLITUDE);
}

static float log_kurtosis(float v)
{
    return logf(fmaxf(v + ANOMALIA_DESLOC_KURTOSIS, 1e-3f));
}

static float log_thd(float v)
{
    return logf((v > 0.0f ? v : 0.0f) + ANOMALIA_EPS_THD);
}

void anomalia_extrair_features(const metricas_t *m, float out[ANOMALIA_N_FEATURES])
{
    if (out == NULL) {
        return;
    }
    if (m == NULL) {
        for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
            out[i] = 0.0f;
        }
        return;
    }
    size_t k = 0;
    for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
        out[k++] = log_amplitude(m->rms[e]);
        out[k++] = log_amplitude(m->harmonica_1x[e]);
        out[k++] = log_amplitude(m->harmonica_2x[e]);
        out[k++] = log_amplitude(m->banda_3x_5x[e]);
        out[k++] = log_kurtosis(m->kurtosis[e]);
        out[k++] = log_thd(m->thd[e]);
    }
}

float anomalia_score(const anomalia_modelo_t *modelo, const metricas_t *m)
{
    if (modelo == NULL || m == NULL) {
        return NAN;
    }
    float f[ANOMALIA_N_FEATURES];
    anomalia_extrair_features(m, f);

    float z[ANOMALIA_N_FEATURES];
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        const float escala = modelo->escala[i] > 0.0f ? modelo->escala[i] : 1.0f;
        z[i] = (f[i] - modelo->media[i]) / escala;
    }

    double d2 = 0.0;
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        double linha = 0.0;
        for (size_t j = 0; j < ANOMALIA_N_FEATURES; ++j) {
            linha += (double)modelo->precisao[i][j] * z[j];
        }
        d2 += z[i] * linha;
    }
    if (!isfinite(d2)) {
        return NAN;
    }
    return d2 > 0.0 ? (float)sqrt(d2) : 0.0f;
}

bool anomalia_eh_anomalia(const anomalia_modelo_t *modelo, float score)
{
    return modelo != NULL && isfinite(score) && score > modelo->limiar;
}

const anomalia_modelo_t *anomalia_modelo_embarcado(void)
{
#if MODELO_ANOMALIA_DISPONIVEL
    return &MODELO_ANOMALIA;
#else
    return NULL;
#endif
}
