/*
 * Testes Unity do componente signal_processing — corpus executado ON-TARGET
 * (app de teste `test_app`, componente `unity` do ESP-IDF):
 *
 *   idf.py -C test_app build flash monitor
 *   → resumo Unity no serial (N Tests, 0 Failures, OK)
 *
 * Convenção (API core do Unity, sem macros específicas do IDF): as funções de
 * teste são públicas e registradas em `rodar_testes_signal_processing()`,
 * chamada pelo runner entre UNITY_BEGIN()/UNITY_END().
 *
 * setUp()/tearDown() vivem AQUI. Quando surgirem outros test_*.c no projeto,
 * movê-los para um arquivo comum único (símbolo duplicado senão).
 *
 * Casos cobertos:
 *   RMS (ticket 01):
 *     1. Senoide pura com períodos inteiros na janela → RMS = A/√2 (por eixo).
 *     2. Sinal DC (gravidade em repouso)              → RMS = |DC|.
 *     3. Senoide + DC                                 → √(A²/2 + DC²).
 *     4. Janela zerada / nula / métricas nulas        → 0, defensivos.
 *     5. Janela parcial (n_amostras < N)              → RMS sobre n dado.
 *     6. n_amostras > N é limitado (sem ler fora do buffer).
 *     7. Utilitários calcular_rms / calcular_rms_passo.
 *   Cadeia espectral (ticket 02) — tolerâncias calibradas numericamente
 *   (protótipo float32 host, DFT naive = pior caso que o FFT do esp-dsp):
 *     8. Senoide pura em f0 (bin alinhado)  → 1x ≈ A, demais harmônicos ≈ 0,
 *        THD ≈ 0.
 *     9. Senoide em 2f0                     → 2x ≈ A, 1x ≈ 0, THD = 0
 *        (guarda: fundamental ausente → 0, não razão entre vazamentos).
 *    10. Senoide em 4f0                     → banda 3x–5x ≈ A, 1x/2x ≈ 0.
 *    11. Composta f0 + 3f0 (0,5·A)          → THD ≈ 0,5 (definição exata).
 *    12. Isolamento por eixo                → espectro de um eixo não vaza.
 *    13. f0 inválido (≤ 0)                  → espectrais 0; RMS/kurtosis ok.
 *    14. DC (gravidade)                     → 1x ≈ 0, THD = 0 (guarda).
 *    15. f0 acima de Nyquist/2              → 1x com scalloping conhecido;
 *        2x/banda fora de Nyquist → 0.
 *    16. Kurtosis: senoide → −1,5; impulsiva → alta (≫ 3); DC → 0;
 *        utilitário direto ({−1,+1} → −2) e defensivos.
 *
 * Frequências de teste: f0 = 31,25 Hz = 32 · (500/512) — cai EXATAMENTE no
 * bin 32 da FFT-512; harmônicos 2x..5x caem nos bins 64/96/128/160. Com a
 * Hann aplicada, tom em bin alinhado concentra a energia no bin + 2 vizinhos
 * (vazamento teoricamente nulo nos bins harmônicos distantes).
 */
#include "unity.h"

#include "signal_processing.h"

#include <math.h>

/* M_PI não é padrão em -std=c11 estrito. */
#define PI 3.14159265358979323846f

/* f0 do teste: 31,25 Hz → bin 32 exato (32·500/512). RPM equivalente: 1875. */
#define F0_TESTE 31.25f

/*
 * Buffers de teste FORA da pilha: janela_t tem ~4,8 KB — na pilha estouraria a
 * task main do IDF (3,5 KB por padrão) e pilhas host limitadas. Os testes rodam
 * sequencialmente numa única thread e ambos os buffers são completamente
 * (re)inicializados antes de cada leitura (preencher_senoide zera tudo;
 * analisar_janela escreve todas as métricas), então o compartilhamento é seguro.
 */
static janela_t j;
static metricas_t m;

void setUp(void)
{
    /* Nenhum estado global entre testes (componente é puro); os buffers j/m
     * acima são sempre totalmente reinicializados por cada teste antes do uso. */
}

void tearDown(void)
{
}

/* Preenche a janela com zeros e, no eixo indicado, com
 * x(t) = dc + A·sen(2π·f·t), fs = 500 Hz (normativo), n amostras. */
static void preencher_senoide(janela_t *j, eixo_t eixo, float amplitude, float freq_hz,
                              float dc, uint32_t n)
{
    const float fs = JANELA_FS_NOMINAL_HZ; /* Normativo: fs = 500 Hz, janela de 1 s (N=500). */
    j->n_amostras = n;
    j->ts_primeira_us = 1000000;
    j->ts_ultima_us = 1000000 + (uint64_t)((n - 1) * 1000000.0f / fs);
    for (uint32_t i = 0; i < JANELA_N_AMOSTRAS; ++i) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            j->amostras[i][e] = 0.0f;
        }
    }
    for (uint32_t i = 0; i < n && i < JANELA_N_AMOSTRAS; ++i) {
        float t = (float)i / fs;
        j->amostras[i][eixo] = dc + amplitude * sinf(2.0f * PI * freq_hz * t);
    }
}

/* SOMA uma senoide ao eixo (para sinais compostos, ex.: f0 + 3f0 do THD). */
static void somar_senoide(janela_t *j, eixo_t eixo, float amplitude, float freq_hz,
                          uint32_t n)
{
    const float fs = JANELA_FS_NOMINAL_HZ;
    for (uint32_t i = 0; i < n && i < JANELA_N_AMOSTRAS; ++i) {
        float t = (float)i / fs;
        j->amostras[i][eixo] += amplitude * sinf(2.0f * PI * freq_hz * t);
    }
}

/* Preenche TODAS as métricas com lixo (para provar que a análise sobrescreve). */
static void sujar_metricas(metricas_t *m)
{
    float *p = &m->rms[0];
    for (size_t i = 0; i < sizeof(metricas_t) / sizeof(float); ++i) {
        p[i] = 99.0f;
    }
}

/* ============================ RMS (ticket 01) ============================ */

/* 1. Senoide pura 10 Hz, A=2 m/s², 10 períodos inteiros em 500 amostras (1 s). */
void test_rms_senoide_pura_eixo_x(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f / sqrtf(2.0f), m.rms[EIXO_X]);
}

void test_senoide_em_um_eixo_nao_vaza_para_outros(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
}

/* 2. DC de gravidade em repouso: RMS = |DC|. */
void test_rms_dc_gravidade(void)
{
    preencher_senoide(&j, EIXO_Z, 0.0f, 10.0f, -9.81f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.81f, m.rms[EIXO_Z]);
}

/* 3. Senoide sobre DC: RMS = √(A²/2 + DC²). */
void test_rms_senoide_sobre_dc(void)
{
    const float esperado = sqrtf((2.0f * 2.0f) / 2.0f + 9.81f * 9.81f);
    preencher_senoide(&j, EIXO_Y, 2.0f, 10.0f, 9.81f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, esperado, m.rms[EIXO_Y]);
}

/* 4a. Janela toda em zero → RMS 0 (e todas as demais métricas 0). */
void test_janela_zerada_rms_zero(void)
{
    preencher_senoide(&j, EIXO_X, 0.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.thd[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_X]);
}

/* 4b. Janela NULL → métricas zeradas (defensivo). */
void test_analisar_janela_nula_zera_metricas(void)
{
    sujar_metricas(&m);
    analisar_janela(NULL, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.harmonica_1x[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.banda_3x_5x[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.thd[EIXO_Y]);
}

/* 4c. Métricas NULL → no-op (não deve falhar). */
void test_analisar_janela_metricas_nulas_e_no_op(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, NULL); /* Se retornar, passou. */
    TEST_PASS_MESSAGE("no-op com metricas_out NULL");
}

/* 5. Janela parcial: 200 amostras de senoide A=1 (4 períodos a 500 Hz) → A/√2. */
void test_janela_parcial_usa_n_amostras(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, 200);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f / sqrtf(2.0f), m.rms[EIXO_X]);
    /* Kurtosis sobre as 200 amostras (4 ciclos inteiros) continua −1,5. */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -1.5f, m.kurtosis[EIXO_X]);
}

/* 6. n_amostras > JANELA_N_AMOSTRAS é limitado — sem ler fora do buffer.
 *    Com todo o buffer preenchido por senoide A=1, RMS deve ser A/√2;
 *    se o limite não existisse, a leitura além de 400 seria indefinida. */
void test_n_amostras_acima_do_maximo_e_limitado(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    j.n_amostras = JANELA_N_AMOSTRAS + 100; /* inválido de propósito */
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f / sqrtf(2.0f), m.rms[EIXO_X]);
}

/* 7a. Utilitário direto: RMS({3,4}) = 5/√2. */
void test_calcular_rms_direto(void)
{
    const float v[2] = {3.0f, 4.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f / sqrtf(2.0f), calcular_rms(v, 2));
}

/* 7b. Utilitários defensivos: NULL, n=0, passo=0 → 0.0f. */
void test_calcular_rms_defensivos(void)
{
    const float v[2] = {3.0f, 4.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_rms(NULL, 2));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_rms(v, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_rms_passo(v, 2, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f / sqrtf(2.0f), calcular_rms_passo(v, 2, 1));
}

/* ===================== Cadeia espectral (ticket 02) ===================== */

/* 8. Senoide pura em f0 (bin 32 alinhado), A=2 → 1x = A; vazamento nos demais
 *    harmônicos/banda ≈ 0; THD ≈ 0. */
void test_espectral_senoide_em_f0(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.02f * 2.0f, 2.0f, m.harmonica_1x[EIXO_X]); /* 1% obs. */
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_2x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.thd[EIXO_X]);
    /* 1x domina o espectro harmônico. */
    TEST_ASSERT_GREATER_THAN_FLOAT(100.0f * m.banda_3x_5x[EIXO_X],
                                   m.harmonica_1x[EIXO_X]);
}

/* 9. Senoide em 2f0 (bin 64), A=1,5 → 2x = A; 1x ≈ 0 e THD = 0 (guarda da
 *    fundamental ausente — sem a guarda, THD seria razão entre vazamentos). */
void test_espectral_senoide_em_2f0(void)
{
    preencher_senoide(&j, EIXO_X, 1.5f, 2.0f * F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.02f * 1.5f, 1.5f, m.harmonica_2x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.banda_3x_5x[EIXO_X]);
}

/* 10. Senoide em 4f0 (bin 128, dentro da banda [3f0, 5f0]) → banda ≈ A. */
void test_espectral_banda_3x_5x(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 4.0f * F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_2x[EIXO_X]);
}

/* 11. Composta: A·sen(f0) + (A/2)·sen(3f0) → THD = √(V₃²)/V₁ = 0,5 exato;
 *     banda 3x–5x captura o 3º harmônico (A/2); RMS confere √(A²/2 + (A/2)²/2). */
void test_espectral_thd_terceiro_harmonico(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    somar_senoide(&j, EIXO_X, 1.0f, 3.0f * F0_TESTE, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.02f * 2.0f, 2.0f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, m.thd[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, sqrtf(2.0f + 0.5f), m.rms[EIXO_X]);
}

/* 12. Isolamento por eixo: espectro calculado no eixo Y não vaza para X/Z. */
void test_espectral_isolamento_por_eixo(void)
{
    preencher_senoide(&j, EIXO_Y, 2.0f, F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.02f * 2.0f, 2.0f, m.harmonica_1x[EIXO_Y]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.harmonica_1x[EIXO_Z]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, m.banda_3x_5x[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_Z]);
}

/* 13. f0 inválido (≤ 0) → métricas espectrais zeradas; RMS e kurtosis
 *     (que não dependem de f0) permanecem corretos. Senoide a 10 Hz (10
 *     ciclos inteiros) para expectativas exatas — o conteúdo do sinal é
 *     irrelevante aqui, pois nenhuma métrica espectral é calculada. */
void test_espectral_f0_invalido(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, 0.0f, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.harmonica_2x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f / sqrtf(2.0f), m.rms[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -1.5f, m.kurtosis[EIXO_X]);
}

/* 14. DC de gravidade: sem fundamental (1x ≈ vazamento de DC ≈ 10⁻⁴) e THD = 0
 *     pela guarda (V₁ ≤ 10⁻³·RMS); kurtosis = 0 (variância nula). */
void test_espectral_dc_sem_fundamental(void)
{
    preencher_senoide(&j, EIXO_Z, 0.0f, F0_TESTE, -9.81f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-2f, m.harmonica_1x[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_Z]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_Z]);
}

/* 15. f0 = 200 Hz (bin 204,8 → 205, scalloping de Hann ≈ −0,35 dB): 1x dentro
 *     de 3%; 2x e banda acima de Nyquist → 0; THD → 0. */
void test_espectral_f0_proximo_de_nyquist(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 200.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, 200.0f, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 1.0f, m.harmonica_1x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.harmonica_2x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.banda_3x_5x[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, m.thd[EIXO_X]);
}

/* ================================ Kurtosis =============================== */

/* 16a. Senoide pura → excesso de Fisher = −1,5 (momento teórico exato). */
void test_kurtosis_senoide_menos_1_5(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -1.5f, m.kurtosis[EIXO_X]);
}

/* 16b. Sinal impulsivo (10 amostras em 10, resto zero) → kurtosis ≫ 3
 *      (teórico ≈ 45; protótipo: 45,02). */
void test_kurtosis_impulsiva_elevada(void)
{
    preencher_senoide(&j, EIXO_X, 0.0f, F0_TESTE, 0.0f, JANELA_N_AMOSTRAS);
    for (int i = 200; i < 210; ++i) {
        j.amostras[i][EIXO_X] = 10.0f;
    }
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_GREATER_THAN_FLOAT(20.0f, m.kurtosis[EIXO_X]);
    /* RMS da rajada confere: √(10·10²/500) = √2. */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sqrtf(2.0f), m.rms[EIXO_X]);
}

/* 16c. DC constante → variância 0 → kurtosis definida como 0. */
void test_kurtosis_dc_zero(void)
{
    preencher_senoide(&j, EIXO_X, 0.0f, F0_TESTE, 5.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, F0_TESTE, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.kurtosis[EIXO_X]);
}

/* 16d. Utilitário direto: {−1,+1} → m2 = m4 = 1 → excesso = −2 (exato);
 *      par de pontos distintos tem curtose mínima. */
void test_calcular_kurtosis_direto(void)
{
    const float v[2] = {-1.0f, 1.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, calcular_kurtosis(v, 2));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, calcular_kurtosis_passo(v, 2, 1));
    /* Com passo, ignora elementos intercalados: {3, 99, 4, 99}, passo 2
     * → amostras {3,4} → −2. */
    const float w[4] = {3.0f, 99.0f, 4.0f, 99.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -2.0f, calcular_kurtosis_passo(w, 2, 2));
}

/* 16e. Defensivos: NULL, n=0, passo=0, constante → 0.0f. */
void test_calcular_kurtosis_defensivos(void)
{
    const float v[2] = {-1.0f, 1.0f};
    const float c[3] = {2.0f, 2.0f, 2.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_kurtosis(NULL, 2));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_kurtosis(v, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, calcular_kurtosis_passo(v, 2, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, calcular_kurtosis(c, 3));
}

/* Registro dos testes — chamado pelos runners entre UNITY_BEGIN/UNITY_END. */
void rodar_testes_signal_processing(void)
{
    /* RMS (ticket 01) */
    RUN_TEST(test_rms_senoide_pura_eixo_x);
    RUN_TEST(test_senoide_em_um_eixo_nao_vaza_para_outros);
    RUN_TEST(test_rms_dc_gravidade);
    RUN_TEST(test_rms_senoide_sobre_dc);
    RUN_TEST(test_janela_zerada_rms_zero);
    RUN_TEST(test_analisar_janela_nula_zera_metricas);
    RUN_TEST(test_analisar_janela_metricas_nulas_e_no_op);
    RUN_TEST(test_janela_parcial_usa_n_amostras);
    RUN_TEST(test_n_amostras_acima_do_maximo_e_limitado);
    RUN_TEST(test_calcular_rms_direto);
    RUN_TEST(test_calcular_rms_defensivos);
    /* Cadeia espectral (ticket 02) */
    RUN_TEST(test_espectral_senoide_em_f0);
    RUN_TEST(test_espectral_senoide_em_2f0);
    RUN_TEST(test_espectral_banda_3x_5x);
    RUN_TEST(test_espectral_thd_terceiro_harmonico);
    RUN_TEST(test_espectral_isolamento_por_eixo);
    RUN_TEST(test_espectral_f0_invalido);
    RUN_TEST(test_espectral_dc_sem_fundamental);
    RUN_TEST(test_espectral_f0_proximo_de_nyquist);
    /* Kurtosis */
    RUN_TEST(test_kurtosis_senoide_menos_1_5);
    RUN_TEST(test_kurtosis_impulsiva_elevada);
    RUN_TEST(test_kurtosis_dc_zero);
    RUN_TEST(test_calcular_kurtosis_direto);
    RUN_TEST(test_calcular_kurtosis_defensivos);
}
