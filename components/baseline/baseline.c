/*
 * baseline — núcleo puro (ver baseline.h).
 *
 * Calibração: média e σ incrementais por (métrica, eixo) via Welford —
 * uma passada, acumulação em double, sem guardar as janelas:
 *
 *   n ← n + 1
 *   δ   = x − media
 *   media += δ / n
 *   M2    += δ · (x − media)          (M2 = Σ(x−média)²)
 *
 * ao completar 30 janelas: σ = √(M2/n) (populacional, ÷ n — coerente com
 * os momentos viesados da kurtosis do signal_processing).
 *
 * Registro de persistência: mágica + versão + floats + CRC32, tudo aqui
 * (puro); a NVS apenas carrega/grava os bytes (baseline_nvs.c).
 */
#include "baseline.h"

#include <math.h>
#include <string.h>

/* Mágica "PNB1" (PulsoPNAAT Baseline, rev. 1) e versão do layout. */
static const uint8_t MAGICA[4] = { 'P', 'N', 'B', '1' };
#define VERSAO_REGISTRO 1

void baseline_calibracao_iniciar(baseline_calibracao_t *cal)
{
    if (cal == NULL) {
        return;
    }
    memset(cal, 0, sizeof(*cal));
}

bool baseline_calibracao_adicionar(baseline_calibracao_t *cal,
                                   const metricas_t *metricas)
{
    if (cal == NULL || metricas == NULL) {
        return false;
    }
    if (cal->n >= BASELINE_N_JANELAS) {
        return true; /* coleta já completa — janelas excedentes ignoradas */
    }

    float v[METRICA_NUM][JANELA_NUM_EIXOS];
    metricas_para_vetor(metricas, v);

    for (int m = 0; m < METRICA_NUM; ++m) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            const double x = (double)v[m][e];
            const double delta = x - cal->media[m][e];
            cal->media[m][e] += delta / (double)(cal->n + 1);
            cal->m2[m][e] += delta * (x - cal->media[m][e]);
        }
    }
    cal->n++;

    return cal->n >= BASELINE_N_JANELAS;
}

bool baseline_calibracao_completa(const baseline_calibracao_t *cal)
{
    return cal != NULL && cal->n >= BASELINE_N_JANELAS;
}

bool baseline_calibracao_extrair(const baseline_calibracao_t *cal,
                                 baseline_t *out)
{
    if (cal == NULL || out == NULL || !baseline_calibracao_completa(cal)) {
        return false;
    }

    for (int m = 0; m < METRICA_NUM; ++m) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            out->media[m][e] = (float)cal->media[m][e];
            out->desvio_padrao[m][e] =
                (float)sqrt(cal->m2[m][e] / (double)cal->n);
        }
    }
    return true;
}

bool baseline_valido(const baseline_t *b)
{
    if (b == NULL) {
        return false;
    }
    for (int m = 0; m < METRICA_NUM; ++m) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            if (!isfinite(b->media[m][e]) || !isfinite(b->desvio_padrao[m][e]) ||
                b->desvio_padrao[m][e] < 0.0f) {
                return false;
            }
        }
    }
    return true;
}

/* CRC32 IEEE (refletido, poly 0xEDB88320, init/final 0xFFFFFFFF) — 32 bits
 * suficientes para detectar corrupção do blob de 156 bytes na NVS. */
static uint32_t crc32_ieee(const uint8_t *dados, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= dados[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

void baseline_empacotar(const baseline_t *b, uint8_t *pacote)
{
    if (pacote == NULL || b == NULL) {
        return;
    }

    memset(pacote, 0, BASELINE_TAM_PACOTE);
    memcpy(pacote, MAGICA, sizeof(MAGICA));

    const uint32_t versao = VERSAO_REGISTRO;
    memcpy(&pacote[4], &versao, sizeof(versao));

    memcpy(&pacote[8], b->media, sizeof(b->media));
    memcpy(&pacote[8 + sizeof(b->media)], b->desvio_padrao,
           sizeof(b->desvio_padrao));

    const size_t tam_payload = 8 + sizeof(b->media) + sizeof(b->desvio_padrao);
    const uint32_t crc = crc32_ieee(pacote, tam_payload);
    memcpy(&pacote[tam_payload], &crc, sizeof(crc));
}

bool baseline_desempacotar(const uint8_t *pacote, baseline_t *out)
{
    if (pacote == NULL || out == NULL) {
        return false;
    }

    if (memcmp(pacote, MAGICA, sizeof(MAGICA)) != 0) {
        return false;
    }

    uint32_t versao = 0;
    memcpy(&versao, &pacote[4], sizeof(versao));
    if (versao != VERSAO_REGISTRO) {
        return false;
    }

    baseline_t lido;
    memcpy(lido.media, &pacote[8], sizeof(lido.media));
    memcpy(lido.desvio_padrao, &pacote[8 + sizeof(lido.media)],
           sizeof(lido.desvio_padrao));

    const size_t tam_payload = 8 + sizeof(lido.media) + sizeof(lido.desvio_padrao);
    uint32_t crc_gravado = 0;
    memcpy(&crc_gravado, &pacote[tam_payload], sizeof(crc_gravado));
    if (crc32_ieee(pacote, tam_payload) != crc_gravado) {
        return false;
    }

    if (!baseline_valido(&lido)) {
        return false;
    }

    *out = lido;
    return true;
}
