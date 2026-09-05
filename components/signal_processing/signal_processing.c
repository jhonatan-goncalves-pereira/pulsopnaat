/*
 * signal_processing — implementação (ver signal_processing.h).
 *
 * Cadeia espectral por janela e por eixo (ticket 02):
 *
 *   amostras do eixo (n ≤ 500)
 *     → janela de Hann (dsps_wind_hann_f32, simétrica — w[0]=w[n−1]=0)
 *     → zero-padding para 512 (parte imaginária = 0)
 *     → FFT complexa radix-2 do esp-dsp (dsps_fft2r_fc32 + bit-reverse)
 *     → bins 0..256 contêm o espectro do sinal real (metade superior é o
 *       espelho Hermitiano — por isso NÃO se usa dsps_cplx2reC_fc32, que é
 *       o truque para DUAS reais numa FFT e dobraria os bins aqui)
 *     → amplitude física no bin = 2·|X[k]|/Σw (Σw = soma da janela aplicada,
 *       embutindo o ganho coerente da Hann — exato para tom em bin alinhado,
 *       qualquer que seja n)
 *     → harmônicos 1x..5x no bin MAIS PRÓXIMO de k·f0 (SPEC: "magnitude
 *       espectral no bin correspondente"); banda 3x–5x = máximo em
 *       [3·f0, 5·f0]; THD sobre 1x–5x; kurtosis direto nas amostras.
 *
 * Sem ESP-IDF core, sem FreeRTOS, sem I/O, sem alocação: os buffers abaixo
 * são rascunho estático (single-consumer, não reentrante) e a única
 * dependência é o esp-dsp — biblioteca de cálculo puro.
 */
#include "signal_processing.h"

#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"
#include "esp_err.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* A visão tabular [métrica][eixo] de metricas_t pressupõe que a struct é
 * exatamente METRICA_NUM arrays de JANELA_NUM_EIXOS floats (header documenta
 * a correspondência com metrica_id_t). Se alguém acrescentar uma métrica sem
 * atualizar o enum (ou vice-versa), o build quebra aqui — não na bancada. */
_Static_assert(sizeof(metricas_t) ==
                   (size_t)METRICA_NUM * JANELA_NUM_EIXOS * sizeof(float),
               "metricas_t deve ter METRICA_NUM x JANELA_NUM_EIXOS floats");

/* Buffers de rascunho — alinhados a 16 bytes como os exemplos oficiais do
 * esp-dsp (caminho SIMD aes3/ae32 da FFT). Não reentrante por design: uma
 * única task de processamento consome as janelas (SPEC §Arquitetura). */
static _Alignas(16) float s_hann[JANELA_N_AMOSTRAS];
static _Alignas(16) float s_fft[2 * ANALISE_FFT_N]; /* intercalado Re,Im */

void signal_processing_init(void)
{
    /* Padrão dos exemplos oficiais do esp-dsp: tabela de twiddle cobrindo
     * CONFIG_DSP_MAX_FFT_SIZE (default 4096 ≥ 512), alocada internamente.
     * Idempotente (dsps_fft2r_initialized). Se falhar (heap), aborta no
     * boot — coerente com o estilo ESP_ERROR_CHECK do app_main. */
    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));
}

void metricas_para_vetor(const metricas_t *metricas_out,
                         float saida[METRICA_NUM][JANELA_NUM_EIXOS])
{
    if (saida == NULL) {
        return;
    }
    if (metricas_out == NULL) {
        memset(saida, 0, (size_t)METRICA_NUM * JANELA_NUM_EIXOS * sizeof(float));
        return;
    }
    memcpy(saida[0], metricas_out->rms, sizeof(float) * JANELA_NUM_EIXOS);
    memcpy(saida[1], metricas_out->harmonica_1x, sizeof(float) * JANELA_NUM_EIXOS);
    memcpy(saida[2], metricas_out->harmonica_2x, sizeof(float) * JANELA_NUM_EIXOS);
    memcpy(saida[3], metricas_out->banda_3x_5x, sizeof(float) * JANELA_NUM_EIXOS);
    memcpy(saida[4], metricas_out->kurtosis, sizeof(float) * JANELA_NUM_EIXOS);
    memcpy(saida[5], metricas_out->thd, sizeof(float) * JANELA_NUM_EIXOS);
}

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

float calcular_kurtosis(const float *amostras, size_t n_amostras)
{
    return calcular_kurtosis_passo(amostras, n_amostras, 1);
}

float calcular_kurtosis_passo(const float *amostras, size_t n_amostras, size_t passo)
{
    if (amostras == NULL || n_amostras == 0 || passo == 0) {
        return 0.0f;
    }

    double soma = 0.0;
    const float *p = amostras;
    for (size_t i = 0; i < n_amostras; ++i) {
        soma += (double)(*p);
        p += passo;
    }
    const double media = soma / (double)n_amostras;

    /* Momentos centrais viesados (divisão por n) — definição canônica de
     * kurtosis em análise de vibração; acumulação em double. */
    double m2 = 0.0, m4 = 0.0;
    p = amostras;
    for (size_t i = 0; i < n_amostras; ++i) {
        const double d = (double)(*p) - media;
        m2 += d * d;
        m4 += d * d * d * d;
        p += passo;
    }
    m2 /= (double)n_amostras;
    m4 /= (double)n_amostras;

    /* Sinal constante (variância 0) → sem impulsividade mensurável → 0
     * (evita divisão por zero; DC da gravidade em repouso cai aqui). */
    if (m2 <= 0.0) {
        return 0.0f;
    }
    return (float)(m4 / (m2 * m2) - 3.0);
}

/* Amplitude física (m/s²) do bin `bin` do espectro já em ordem natural:
 * 2·|X[k]|/Σw — o fator 2 recupera a metade de energia do espectro de um
 * sinal real e Σw embute o ganho coerente da janela aplicada. */
static inline float amplitude_no_bin(int bin, float soma_w)
{
    const float re = s_fft[2 * bin];
    const float im = s_fft[2 * bin + 1];
    return 2.0f * sqrtf(re * re + im * im) / soma_w;
}

/* Extrai as amplitudes harmônicas 1x–5x (bin mais próximo de k·f0) e a
 * banda 3x–5x (máximo em [3·f0, 5·f0]) do espectro já calculado em s_fft.
 * Harmônicos acima de Nyquist e bins fora da faixa contribuem com zero. */
static void mapear_harmonicos(float f0_hz, float df, float soma_w,
                              float v[ANALISE_N_HARMONICOS + 1], float *banda)
{
    const int bin_nyquist = ANALISE_FFT_N / 2;

    v[0] = 0.0f; /* índice 0 não usado (harmônicos começam em 1) */
    for (int h = 1; h <= ANALISE_N_HARMONICOS; ++h) {
        v[h] = 0.0f;
        int bin = (int)lroundf((float)h * f0_hz / df);
        if (bin < 1) {
            bin = 1; /* f0 abaixo de meio bin → nunca mapear no DC */
        }
        if (bin <= bin_nyquist) {
            v[h] = amplitude_no_bin(bin, soma_w);
        }
    }

    *banda = 0.0f;
    int lo = (int)ceilf(3.0f * f0_hz / df);
    int hi = (int)floorf(5.0f * f0_hz / df);
    if (lo < 1) {
        lo = 1;
    }
    if (hi > bin_nyquist) {
        hi = bin_nyquist;
    }
    for (int bin = lo; bin <= hi; ++bin) {
        const float a = amplitude_no_bin(bin, soma_w);
        if (a > *banda) {
            *banda = a;
        }
    }
}

/* Cadeia espectral completa de um eixo: Hann + zero-padding + FFT + bins
 * harmônicos. Preenche v[1..5] e banda; retorna erro do esp-dsp se houver
 * (chamador zera as saídas e mantém RMS/kurtosis). */
static esp_err_t extrair_espectro_eixo(const janela_t *janela, eixo_t eixo,
                                       uint32_t n, float f0_hz, float fs_eff,
                                       float soma_w,
                                       float v[ANALISE_N_HARMONICOS + 1],
                                       float *banda)
{
    /* Hann sobre as n amostras + montagem do buffer complexo com padding. */
    for (uint32_t i = 0; i < n; ++i) {
        s_fft[2 * i] = janela->amostras[i][eixo] * s_hann[i];
        s_fft[2 * i + 1] = 0.0f;
    }
    memset(&s_fft[2 * n], 0, (ANALISE_FFT_N - n) * 2 * sizeof(float));

    esp_err_t err = dsps_fft2r_fc32(s_fft, ANALISE_FFT_N);
    if (err != ESP_OK) {
        return err;
    }
    err = dsps_bit_rev_fc32(s_fft, ANALISE_FFT_N); /* ordem natural dos bins */
    if (err != ESP_OK) {
        return err;
    }

    mapear_harmonicos(f0_hz, fs_eff / (float)ANALISE_FFT_N, soma_w, v, banda);
    return ESP_OK;
}

void analisar_janela(const janela_t *janela, float f0_hz, metricas_t *metricas_out)
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

    /* fs efetiva medida na janela (timestamps SH-2) — o mapeamento de bins
     * harmônicos usa a taxa REAL do sensor, não a nominal (SPEC). */
    float fs_eff = JANELA_FS_NOMINAL_HZ;
    if (n >= 2 && janela->ts_ultima_us > janela->ts_primeira_us) {
        fs_eff = (float)(n - 1) * 1e6f /
                 (float)(janela->ts_ultima_us - janela->ts_primeira_us);
    }

    /* Janela de Hann: uma única forma de onda por chamada (função de n),
     * reaproveitada pelos três eixos. Σw = ganho coerente real aplicado. */
    bool espectral_ok = (f0_hz > 0.0f) && (n >= 2);
    float soma_w = 0.0f;
    if (espectral_ok) {
        dsps_wind_hann_f32(s_hann, (int)n);
        for (uint32_t i = 0; i < n; ++i) {
            soma_w += s_hann[i];
        }
        espectral_ok = (soma_w > 0.0f); /* n < 3 daria janela nula */
    }

    float v[ANALISE_N_HARMONICOS + 1];

    for (int eixo = 0; eixo < JANELA_NUM_EIXOS; ++eixo) {
        metricas_out->rms[eixo] =
            calcular_rms_passo(&janela->amostras[0][eixo], n, JANELA_NUM_EIXOS);
        metricas_out->kurtosis[eixo] =
            calcular_kurtosis_passo(&janela->amostras[0][eixo], n, JANELA_NUM_EIXOS);
        metricas_out->harmonica_1x[eixo] = 0.0f;
        metricas_out->harmonica_2x[eixo] = 0.0f;
        metricas_out->banda_3x_5x[eixo] = 0.0f;
        metricas_out->thd[eixo] = 0.0f;

        if (!espectral_ok) {
            continue; /* f0 inválido/janela degenerada → só RMS e kurtosis */
        }

        esp_err_t err = extrair_espectro_eixo(janela, (eixo_t)eixo, n, f0_hz,
                                              fs_eff, soma_w, v,
                                              &metricas_out->banda_3x_5x[eixo]);
        if (err != ESP_OK) {
            /* FFT indisponível (ex.: esp-dsp sem init) — métricas espectrais
             * zeradas; RMS/kurtosis, que não dependem da FFT, permanecem. */
            continue;
        }

        metricas_out->harmonica_1x[eixo] = v[1];
        metricas_out->harmonica_2x[eixo] = v[2];

        /* THD = √(V₂²+V₃²+V₄²+V₅²)/V₁, apenas com fundamental detectável
         * (guarda documentada no header). */
        const float v1 = v[1];
        if (v1 > 1e-3f * metricas_out->rms[eixo] && v1 > 0.0f) {
            const float soma_h2_h5 =
                v[2] * v[2] + v[3] * v[3] + v[4] * v[4] + v[5] * v[5];
            metricas_out->thd[eixo] = sqrtf(soma_h2_h5) / v1;
        }
    }
}
