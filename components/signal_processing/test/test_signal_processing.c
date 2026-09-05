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
 *   1. Senoide pura com períodos inteiros na janela → RMS = A/√2 (por eixo).
 *   2. Sinal DC (gravidade em repouso)              → RMS = |DC|.
 *   3. Senoide + DC                                 → √(A²/2 + DC²).
 *   4. Janela zerada / nula / métricas nulas        → 0, defensivos.
 *   5. Janela parcial (n_amostras < N)              → RMS sobre n dado.
 *   6. n_amostras > N é limitado (sem ler fora do buffer).
 *   7. Utilitários calcular_rms / calcular_rms_passo (direto e defensivos).
 */
#include "unity.h"

#include "signal_processing.h"

#include <math.h>

/* M_PI não é padrão em -std=c11 estrito. */
#define PI 3.14159265358979323846f

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
    const float fs = 500.0f; /* Normativo: fs = 500 Hz, janela de 1 s (N=500). */
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

/* 1. Senoide pura 10 Hz, A=2 m/s², 10 períodos inteiros em 500 amostras (1 s). */
void test_rms_senoide_pura_eixo_x(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f / sqrtf(2.0f), m.rms[EIXO_X]);
}

void test_senoide_em_um_eixo_nao_vaza_para_outros(void)
{
    preencher_senoide(&j, EIXO_X, 2.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
}

/* 2. DC de gravidade em repouso: RMS = |DC|. */
void test_rms_dc_gravidade(void)
{
    preencher_senoide(&j, EIXO_Z, 0.0f, 10.0f, -9.81f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.81f, m.rms[EIXO_Z]);
}

/* 3. Senoide sobre DC: RMS = √(A²/2 + DC²). */
void test_rms_senoide_sobre_dc(void)
{
    const float esperado = sqrtf((2.0f * 2.0f) / 2.0f + 9.81f * 9.81f);
    preencher_senoide(&j, EIXO_Y, 2.0f, 10.0f, 9.81f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, esperado, m.rms[EIXO_Y]);
}

/* 4a. Janela toda em zero → RMS 0. */
void test_janela_zerada_rms_zero(void)
{
    preencher_senoide(&j, EIXO_X, 0.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
}

/* 4b. Janela NULL → métricas zeradas (defensivo). */
void test_analisar_janela_nula_zera_metricas(void)
{
    m.rms[0] = 1.0f; m.rms[1] = 2.0f; m.rms[2] = 3.0f;
    analisar_janela(NULL, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, m.rms[EIXO_Z]);
}

/* 4c. Métricas NULL → no-op (não deve falhar). */
void test_analisar_janela_metricas_nulas_e_no_op(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    analisar_janela(&j, NULL); /* Se retornar, passou. */
    TEST_PASS_MESSAGE("no-op com metricas_out NULL");
}

/* 5. Janela parcial: 200 amostras de senoide A=1 (4 períodos a 500 Hz) → A/√2. */
void test_janela_parcial_usa_n_amostras(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, 200);
    analisar_janela(&j, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f / sqrtf(2.0f), m.rms[EIXO_X]);
}

/* 6. n_amostras > JANELA_N_AMOSTRAS é limitado — sem ler fora do buffer.
 *    Com todo o buffer preenchido por senoide A=1, RMS deve ser A/√2;
 *    se o limite não existisse, a leitura além de 400 seria indefinida. */
void test_n_amostras_acima_do_maximo_e_limitado(void)
{
    preencher_senoide(&j, EIXO_X, 1.0f, 10.0f, 0.0f, JANELA_N_AMOSTRAS);
    j.n_amostras = JANELA_N_AMOSTRAS + 100; /* inválido de propósito */
    analisar_janela(&j, &m);
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

/* Registro dos testes — chamado pelos runners entre UNITY_BEGIN/UNITY_END. */
void rodar_testes_signal_processing(void)
{
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
}
