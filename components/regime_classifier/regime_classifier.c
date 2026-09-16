/*
 * regime_classifier — implementação (ver regime_classifier.h).
 *
 * Custo por janela: REGIME_MAX_CLASSES formas quadráticas 18×18 (~1,3 mil
 * multiplicações), desprezível frente à FFT.
 */
#include "regime_classifier.h"

#include <math.h>
#include <stddef.h>

#include "modelo_regime.h"

void regime_classificar(const regime_modelo_t *modelo, const metricas_t *m,
                        regime_resultado_t *out)
{
    if (out == NULL) {
        return;
    }
    out->regime = REGIME_DESCONHECIDO;
    out->distancia = NAN;
    out->estado = ESTADO_EQUIP_VERDE;
    if (modelo == NULL || m == NULL || modelo->n_classes <= 0 ||
        modelo->n_classes > REGIME_MAX_CLASSES) {
        return;
    }

    float f[ANOMALIA_N_FEATURES];
    anomalia_extrair_features(m, f);
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        const float escala = modelo->escala[i] > 0.0f ? modelo->escala[i] : 1.0f;
        f[i] = (f[i] - modelo->media_global[i]) / escala;
    }

    int melhor = -1;
    double melhor_pontuacao = 0.0;
    double melhor_d2 = 0.0;
    for (int k = 0; k < modelo->n_classes; ++k) {
        float d[ANOMALIA_N_FEATURES];
        for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
            d[i] = f[i] - modelo->media[k][i];
        }
        double d2 = 0.0;
        for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
            double linha = 0.0;
            for (size_t j = 0; j < ANOMALIA_N_FEATURES; ++j) {
                linha += (double)modelo->precisao[k][i][j] * d[j];
            }
            d2 += d[i] * linha;
        }
        const double pontuacao = d2 + modelo->log_det[k];
        if (isfinite(pontuacao) && (melhor < 0 || pontuacao < melhor_pontuacao)) {
            melhor = k;
            melhor_pontuacao = pontuacao;
            melhor_d2 = d2;
        }
    }
    if (melhor < 0) {
        return;
    }

    const float distancia = melhor_d2 > 0.0 ? (float)sqrt(melhor_d2) : 0.0f;
    out->regime = modelo->valor[melhor];
    out->distancia = distancia;
    if (modelo->falha[melhor] || distancia > modelo->limiar_critico[melhor]) {
        out->estado = ESTADO_EQUIP_VERMELHO;
    } else if (distancia > modelo->limiar_atencao[melhor]) {
        out->estado = ESTADO_EQUIP_AMARELO;
    }
}

void regime_filtro_iniciar(regime_filtro_t *f)
{
    if (f == NULL) {
        return;
    }
    for (int i = 0; i < REGIME_FILTRO_JANELAS; ++i) {
        f->historico[i] = REGIME_DESCONHECIDO;
    }
    f->n = 0;
    f->proximo = 0;
    f->atual = REGIME_DESCONHECIDO;
}

int regime_filtro_atualizar(regime_filtro_t *f, int regime)
{
    if (f == NULL) {
        return regime;
    }
    f->historico[f->proximo] = regime;
    f->proximo = (f->proximo + 1) % REGIME_FILTRO_JANELAS;
    if (f->n < REGIME_FILTRO_JANELAS) {
        f->n++;
    }

    /* Percorre do mais recente ao mais antigo: em empate de contagem fica o primeiro visto. */
    int moda = regime;
    uint32_t moda_contagem = 0;
    const bool primeira = (f->n == 1);
    for (uint32_t a = 0; a < f->n; ++a) {
        const int candidato = f->historico[(f->proximo + REGIME_FILTRO_JANELAS - 1 - a) % REGIME_FILTRO_JANELAS];
        uint32_t contagem = 0;
        for (uint32_t b = 0; b < f->n; ++b) {
            if (f->historico[(f->proximo + REGIME_FILTRO_JANELAS - 1 - b) % REGIME_FILTRO_JANELAS] == candidato) {
                contagem++;
            }
        }
        if (contagem > moda_contagem) {
            moda = candidato;
            moda_contagem = contagem;
        }
    }
    if (primeira || (moda != f->atual && moda_contagem >= REGIME_FILTRO_MINIMO)) {
        f->atual = moda;
    }
    return f->atual;
}

const regime_modelo_t *regime_modelo_embarcado(void)
{
#if MODELO_REGIME_DISPONIVEL
    return &MODELO_REGIME;
#else
    return NULL;
#endif
}
