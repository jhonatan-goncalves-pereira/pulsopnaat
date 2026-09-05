/*
 * Testes Unity do signal_processing — corpus executado ON-TARGET (test_app):
 *   idf.py -C test_app build flash monitor → "N Tests, 0 Failures, OK"
 *
 * Convenção (API core do Unity): funções de teste públicas registradas em
 * rodar_testes_signal_processing(), chamada pelo runner entre
 * UNITY_BEGIN()/UNITY_END(). setUp()/tearDown() vivem AQUI (único arquivo;
 * símbolo duplicado senão). O índice dos casos é a lista de RUN_TEST no fim.
 *
 * Frequências: f0 = 31,25 Hz = 32 · (500/512) cai EXATAMENTE no bin 32 da
 * FFT-512; harmônicos 2x..5x caem nos bins 64/96/128/160. Com Hann, tom em
 * bin alinhado concentra a energia no bin + 2 vizinhos (vazamento nulo nos
 * bins harmônicos distantes).
 */
#include "unity.h"

#include "signal_processing.h"

#include <math.h>
#include <string.h>

/* M_PI não é padrão em -std=c11 estrito. */
#define PI 3.14159265358979323846f

/* f0 do teste: 31,25 Hz → bin 32 exato (32·500/512). RPM equivalente: 1875. */
#define F0_TESTE 31.25f

/*
 * Buffers de teste FORA da pilha: janela_t tem ~4,8 KB — estouraria a task
 * main do IDF. Testes rodam sequencialmente numa única thread e os buffers
 * são (re)inicializados totalmente antes de cada leitura (preencher_senoide
 * zera tudo; analisar_janela escreve todas as métricas) — compartilhamento
 * seguro.
 */
static janela_t j;
static metricas_t m;

void setUp(void)
{
    /* Sem estado global entre testes (componente puro); buffers j/m acima
     * são sempre reinicializados por cada teste antes do uso. */
}

void tearDown(void)
{
}

/* Preenche a janela com zeros e, no eixo indicado, com
 * x(t) = dc + A·sen(2π·f·t), fs = 500 Hz, n amostras. */
static void preencher_senoide(janela_t *j, eixo_t eixo, float amplitude, float freq_hz,
                              float dc, uint32_t n)
{
    const float fs = JANELA_FS_NOMINAL_HZ;
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

/* 6. n_amostras > JANELA_N_AMOSTRAS é limitado — sem ler fora do buffer. */
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
 *     (que não dependem de f0) permanecem corretos. */
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

/* 14. DC de gravidade: sem fundamental e THD = 0 pela guarda (V₁ ≤ 10⁻³·RMS);
 *     kurtosis = 0 (variância nula). */
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

/* ==================== Visão tabular (ticket 03) ==================== */

/* 17. metricas_para_vetor: campos de metricas_t mapeiam na ordem de
 *     metrica_id_t (rms, 1x, 2x, banda, kurt, thd) × eixos (X, Y, Z). */
void test_metricas_para_vetor_mapeia_campos(void)
{
    metricas_t m;
    float *p = &m.rms[0];
    for (size_t i = 0; i < sizeof(metricas_t) / sizeof(float); ++i) {
        p[i] = (float)i; /* valores 0..17 em ordem de memória */
    }
    float v[METRICA_NUM][JANELA_NUM_EIXOS];
    metricas_para_vetor(&m, v);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v[METRICA_RMS][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v[METRICA_RMS][EIXO_Y]);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, v[METRICA_RMS][EIXO_Z]);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, v[METRICA_HARMONICA_1X][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, v[METRICA_BANDA_3X_5X][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, v[METRICA_KURTOSIS][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(17.0f, v[METRICA_THD][EIXO_Z]);
}

/* 18. metricas_para_vetor: defensivos — saída NULL (no-op) e métricas NULL
 *     (saída zerada). */
void test_metricas_para_vetor_defensivos(void)
{
    metricas_t m;
    memset(&m, 0, sizeof(m));
    m.rms[EIXO_X] = 7.0f;
    float v[METRICA_NUM][JANELA_NUM_EIXOS];

    metricas_para_vetor(&m, NULL); /* não deve crashar */

    memset(v, 0xAA, sizeof(v));
    metricas_para_vetor(NULL, v);
    for (int i = 0; i < METRICA_NUM; ++i) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            TEST_ASSERT_EQUAL_FLOAT(0.0f, v[i][e]);
        }
    }
}

/* ------------------------- backstop RNF07 + Hampel (issue 5) ------------------------- */

/* Constrói um float a partir dos seus bits (para injetar 0x60000000 etc.). */
static float float_de_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof(f));
    return f;
}

static void test_amostra_valida_basico(void)
{
    /* Valores plausíveis. */
    TEST_ASSERT_TRUE(amostra_valida(0.0f));
    TEST_ASSERT_TRUE(amostra_valida(9.80665f));      /* gravidade */
    TEST_ASSERT_TRUE(amostra_valida(-9.80665f));
    TEST_ASSERT_TRUE(amostra_valida(100.0f));       /* ~10 g, dentro do limite */
    TEST_ASSERT_TRUE(amostra_valida(-100.0f));
    /* Limite exato (20 g) — inclusivo. */
    TEST_ASSERT_TRUE(amostra_valida(AMOSTRA_LIMITE_FISICO_MPS2));
    TEST_ASSERT_TRUE(amostra_valida(-AMOSTRA_LIMITE_FISICO_MPS2));
    /* Acima do limite — rejeita. */
    TEST_ASSERT_FALSE(amostra_valida(AMOSTRA_LIMITE_FISICO_MPS2 + 0.001f));
    TEST_ASSERT_FALSE(amostra_valida(-AMOSTRA_LIMITE_FISICO_MPS2 - 0.001f));
    TEST_ASSERT_FALSE(amostra_valida(1000.0f));
    /* Caso extremo 0x60000000 (~3,7e19) — rejeita. */
    TEST_ASSERT_FALSE(amostra_valida(float_de_bits(0x60000000u)));
    /* Não-finitos — rejeita. */
    TEST_ASSERT_FALSE(amostra_valida(INFINITY));
    TEST_ASSERT_FALSE(amostra_valida(-INFINITY));
    TEST_ASSERT_FALSE(amostra_valida(NAN));
}

/* Glitch plausível (3 m/s² ≈ 0,3 g) numa senoide de baixa amplitude é
 * detectado e substituído pela mediana local; a senoide limpa não é tocada. */
static void test_hampel_remove_impulso_isolado(void)
{
    preencher_senoide(&j, EIXO_X, 0.5f, 25.0f, 0.0f, JANELA_N_AMOSTRAS);
    const uint32_t ig = 250;
    const float glitch = 3.0f;
    j.amostras[ig][EIXO_X] = glitch;

    const uint32_t n = janela_hampel(&j, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
    TEST_ASSERT_GREATER_THAN(0, n);            /* substituiu ao menos o glitch */
    TEST_ASSERT_NOT_EQUAL(glitch, j.amostras[ig][EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.0f, j.amostras[ig][EIXO_X]);
}

/* Senoide limpa não é danificada: RMS preservado dentro de 5%. */
static void test_hampel_preserva_senoide_limpa(void)
{
    preencher_senoide(&j, EIXO_X, 0.5f, 25.0f, 0.0f, JANELA_N_AMOSTRAS);
    metricas_t antes, depois;
    analisar_janela(&j, 25.0f, &antes);
    janela_t j2 = j;
    const uint32_t n = janela_hampel(&j2, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
    (void)n;
    analisar_janela(&j2, 25.0f, &depois);
    TEST_ASSERT_FLOAT_WITHIN(0.05f * antes.rms[EIXO_X], antes.rms[EIXO_X],
                             depois.rms[EIXO_X]);
}

/* Eixo Z em repouso (gravidade ~9,81) com um glitch de +3 m/s²: removido e a
 * gravidade fica intacta. */
static void test_hampel_preserva_gravidade_com_glitch(void)
{
    preencher_senoide(&j, EIXO_Z, 0.0f, 0.0f, 9.80665f, JANELA_N_AMOSTRAS);
    const uint32_t ig = 250;
    j.amostras[ig][EIXO_Z] = 9.80665f + 3.0f;
    const uint32_t n = janela_hampel(&j, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.80665f, j.amostras[ig][EIXO_Z]);
    TEST_ASSERT_EQUAL_FLOAT(9.80665f, j.amostras[0][EIXO_Z]);
    TEST_ASSERT_EQUAL_FLOAT(9.80665f, j.amostras[JANELA_N_AMOSTRAS - 1][EIXO_Z]);
}

/* Vibracao rica (f0 + harmonicos ate 5x, assinatura real de falha mista)
 * NÃO e comida pelo Hampel: 0 substituicoes com t=5. Regressao do parametro
 * (se alguem baixar o limiar, este teste quebra). */
static void test_hampel_preserva_vibracao_com_harmonicos(void)
{
    /* f0 = 25 Hz + 2x..5x com amplitudes decrescentes (rms ~0.16, realista). */
    j.n_amostras = JANELA_N_AMOSTRAS;
    j.ts_primeira_us = 1000000;
    j.ts_ultima_us = 1000000 + (uint64_t)((JANELA_N_AMOSTRAS - 1) * 1000000.0f / JANELA_FS_NOMINAL_HZ);
    const float fs = JANELA_FS_NOMINAL_HZ;
    const float f0 = 25.0f;
    for (uint32_t i = 0; i < JANELA_N_AMOSTRAS; ++i) {
        float t = (float)i / fs;
        float v = 0.16f * (sinf(2.0f * PI * f0 * t)
                         + 0.5f * sinf(2.0f * PI * 2 * f0 * t)
                         + 0.25f * sinf(2.0f * PI * 3 * f0 * t)
                         + 0.125f * sinf(2.0f * PI * 4 * f0 * t)
                         + 0.0625f * sinf(2.0f * PI * 5 * f0 * t));
        j.amostras[i][EIXO_X] = v;
        j.amostras[i][EIXO_Y] = 0.0f;
        j.amostras[i][EIXO_Z] = 0.0f;
    }
    const uint32_t n = janela_hampel(&j, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
    TEST_ASSERT_LESS_THAN(3, n);  /* quase nada — nao come vibracao real */
}

/* Caso extremo 0x60000000 também é removido pelo Hampel (defesa em camada). */
static void test_hampel_remove_0x60000000(void)
{
    preencher_senoide(&j, EIXO_X, 0.5f, 25.0f, 0.0f, JANELA_N_AMOSTRAS);
    const uint32_t ig = 100;
    const float extremo = float_de_bits(0x60000000u);
    j.amostras[ig][EIXO_X] = extremo;
    const uint32_t n = janela_hampel(&j, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_EQUAL(extremo, j.amostras[ig][EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.0f, j.amostras[ig][EIXO_X]);
}

/* Defensivos: NULL/k<=0/t<=0/n<3 → no-op (0 substituições). */
static void test_hampel_defensivos(void)
{
    preencher_senoide(&j, EIXO_X, 0.5f, 25.0f, 0.0f, JANELA_N_AMOSTRAS);
    TEST_ASSERT_EQUAL(0, janela_hampel(NULL, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD));
    TEST_ASSERT_EQUAL(0, janela_hampel(&j, 0, HAMPEL_LIMIAR_MAD));
    TEST_ASSERT_EQUAL(0, janela_hampel(&j, HAMPEL_K_VIZINHOS, 0.0f));
    TEST_ASSERT_EQUAL(0, janela_hampel(&j, HAMPEL_K_VIZINHOS, -1.0f));
    j.n_amostras = 2;
    TEST_ASSERT_EQUAL(0, janela_hampel(&j, HAMPEL_K_VIZINHOS, HAMPEL_LIMIAR_MAD));
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
    /* Visão tabular (ticket 03) */
    RUN_TEST(test_metricas_para_vetor_mapeia_campos);
    RUN_TEST(test_metricas_para_vetor_defensivos);
    /* Backstop RNF07 + filtro de Hampel (issue 5) */
    RUN_TEST(test_amostra_valida_basico);
    RUN_TEST(test_hampel_remove_impulso_isolado);
    RUN_TEST(test_hampel_preserva_senoide_limpa);
    RUN_TEST(test_hampel_preserva_gravidade_com_glitch);
    RUN_TEST(test_hampel_preserva_vibracao_com_harmonicos);
    RUN_TEST(test_hampel_remove_0x60000000);
    RUN_TEST(test_hampel_defensivos);
}
