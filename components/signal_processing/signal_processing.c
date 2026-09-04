/*
 * signal_processing — implementação pura (ver signal_processing.h).
 *
 * Sem ESP-IDF, sem FreeRTOS, sem alocação: este arquivo compila no host e no
 * alvo com as mesmas regras. Toda a matemática do pipeline de análise vive
 * aqui, para que possa ser testada isoladamente do hardware.
 */
#include "signal_processing.h"

#include <math.h>
#include <string.h>

float calcular_rms_passo(const float *amostras, size_t n_amostras, size_t passo)
{
    if (amostras == NULL || n_amostras == 0 || passo == 0) {
        return 0.0f;
    }

    double soma_quadrados = 0.0;
    const float *p = amostras;
    for (size_t i = 0; i < n_amostras; ++i) {
        soma_quadrados += (double)(*p) * (double)(*p);
        p += passo;
    }

    return (float)sqrt(soma_quadrados / (double)n_amostras);
}

float calcular_rms(const float *amostras, size_t n_amostras)
{
    return calcular_rms_passo(amostras, n_amostras, 1);
}

void analisar_janela(const janela_t *janela, metricas_t *metricas_out)
{
    if (metricas_out == NULL) {
        return;
    }

    metricas_t zerada = {0};
    if (janela == NULL) {
        *metricas_out = zerada;
        return;
    }

    uint32_t n = janela->n_amostras;
    if (n > JANELA_N_AMOSTRAS) {
        n = JANELA_N_AMOSTRAS;
    }

    for (int eixo = 0; eixo < JANELA_NUM_EIXOS; ++eixo) {
        metricas_out->rms[eixo] =
            calcular_rms_passo(&janela->amostras[0][eixo], n, JANELA_NUM_EIXOS);
    }
}
