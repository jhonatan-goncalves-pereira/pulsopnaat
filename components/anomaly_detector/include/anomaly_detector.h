/*
 * anomaly_detector — detector não supervisionado da §5.4: distância de
 * Mahalanobis do vetor de 18 métricas contra a operação saudável. PURO: o
 * modelo é treinado offline (tools/classificador/treinar_detector.py) e só a
 * inferência roda no ESP32, em modo sombra (não decide LED nem alerta).
 */
#pragma once

#include <stdbool.h>

#include "signal_processing.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANOMALIA_N_FEATURES (METRICA_NUM * JANELA_NUM_EIXOS)

/* Transformação log das features — treinar_detector.py espelha estes valores. */
#define ANOMALIA_EPS_AMPLITUDE 1e-3f
#define ANOMALIA_EPS_THD 1e-2f
#define ANOMALIA_DESLOC_KURTOSIS 3.0f

typedef struct {
    float media[ANOMALIA_N_FEATURES];
    float escala[ANOMALIA_N_FEATURES];
    float precisao[ANOMALIA_N_FEATURES][ANOMALIA_N_FEATURES];
    float limiar;
} anomalia_modelo_t;

/* Ordem = colunas do CSV: por eixo (x, y, z): rms, h1x, h2x, b3x5, kurt, thd. */
void anomalia_extrair_features(const metricas_t *m, float out[ANOMALIA_N_FEATURES]);

/* Distância de Mahalanobis no espaço padronizado; NAN com modelo ou métricas NULL. */
float anomalia_score(const anomalia_modelo_t *modelo, const metricas_t *m);

/* score > limiar (estrito); false com modelo NULL ou score não finito. */
bool anomalia_eh_anomalia(const anomalia_modelo_t *modelo, float score);

/* Modelo gerado em modelo_anomalia.h; NULL enquanto for o placeholder sem treino. */
const anomalia_modelo_t *anomalia_modelo_embarcado(void);

#ifdef __cplusplus
}
#endif
