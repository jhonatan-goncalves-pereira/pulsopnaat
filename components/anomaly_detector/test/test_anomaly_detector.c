/*
 * Testes Unity do anomaly_detector — corpus on-target (test_app). Modelo
 * sintético com precisão identidade e escala 1: o score vira distância
 * euclidiana no espaço das features e dá pra calcular à mão.
 */
#include "unity.h"

#include "anomaly_detector.h"

#include <math.h>
#include <string.h>

static anomalia_modelo_t modelo;
static metricas_t m;
static float f[ANOMALIA_N_FEATURES];

static void preparar_modelo_centrado_na_janela(void)
{
    memset(&modelo, 0, sizeof(modelo));
    memset(&m, 0, sizeof(m));
    for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
        m.rms[e] = 0.1f * (float)(e + 1);
        m.harmonica_1x[e] = 0.02f * (float)(e + 1);
        m.harmonica_2x[e] = 0.01f * (float)(e + 1);
        m.banda_3x_5x[e] = 0.005f * (float)(e + 1);
        m.kurtosis[e] = 0.5f * (float)(e + 1);
        m.thd[e] = 0.3f * (float)(e + 1);
    }
    anomalia_extrair_features(&m, modelo.media);
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        modelo.escala[i] = 1.0f;
        modelo.precisao[i][i] = 1.0f;
    }
    modelo.limiar = 3.0f;
}

static void test_features_ordem_e_transformacao(void)
{
    memset(&m, 0, sizeof(m));
    m.rms[EIXO_X] = 1.0f;
    m.harmonica_2x[EIXO_Y] = 0.5f;
    m.kurtosis[EIXO_Z] = -1.5f;
    m.thd[EIXO_Z] = 2.0f;
    anomalia_extrair_features(&m, f);

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, logf(1.0f + ANOMALIA_EPS_AMPLITUDE), f[0]);   /* rms_x */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, logf(ANOMALIA_EPS_AMPLITUDE), f[6]);          /* rms_y zerado */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, logf(0.5f + ANOMALIA_EPS_AMPLITUDE), f[8]);   /* h2x_y */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, logf(1.5f), f[16]);                           /* kurt_z */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, logf(2.0f + ANOMALIA_EPS_THD), f[17]);        /* thd_z */
}

static void test_features_entradas_invalidas_saem_finitas(void)
{
    memset(&m, 0, sizeof(m));
    m.rms[EIXO_X] = -1.0f;
    m.kurtosis[EIXO_X] = -10.0f;
    m.thd[EIXO_X] = NAN;
    m.harmonica_1x[EIXO_Y] = NAN;
    anomalia_extrair_features(&m, f);
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        TEST_ASSERT_TRUE(isfinite(f[i]));
    }
}

static void test_score_zero_na_media(void)
{
    preparar_modelo_centrado_na_janela();
    const float s = anomalia_score(&modelo, &m);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, s);
    TEST_ASSERT_FALSE(anomalia_eh_anomalia(&modelo, s));
}

static void test_score_identidade_e_distancia_euclidiana(void)
{
    preparar_modelo_centrado_na_janela();
    modelo.media[0] -= 2.0f;
    modelo.media[11] += 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sqrtf(8.0f), anomalia_score(&modelo, &m));
}

static void test_score_escala_padroniza(void)
{
    preparar_modelo_centrado_na_janela();
    modelo.media[7] -= 4.0f;
    modelo.escala[7] = 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, anomalia_score(&modelo, &m));
}

static void test_score_escala_invalida_vira_um(void)
{
    preparar_modelo_centrado_na_janela();
    modelo.media[2] -= 2.0f;
    modelo.escala[2] = 0.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, anomalia_score(&modelo, &m));
}

static void test_score_precisao_cheia(void)
{
    preparar_modelo_centrado_na_janela();
    modelo.media[0] -= 1.0f;
    modelo.media[1] -= 1.0f;
    modelo.precisao[0][1] = 0.5f;
    modelo.precisao[1][0] = 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sqrtf(3.0f), anomalia_score(&modelo, &m));
}

static void test_limiar_estrito_e_invalidos(void)
{
    preparar_modelo_centrado_na_janela();
    TEST_ASSERT_FALSE(anomalia_eh_anomalia(&modelo, 3.0f));
    TEST_ASSERT_TRUE(anomalia_eh_anomalia(&modelo, 3.01f));
    TEST_ASSERT_FALSE(anomalia_eh_anomalia(&modelo, NAN));
    TEST_ASSERT_FALSE(anomalia_eh_anomalia(NULL, 100.0f));
}

static void test_defensivos(void)
{
    preparar_modelo_centrado_na_janela();
    TEST_ASSERT_TRUE(isnan(anomalia_score(NULL, &m)));
    TEST_ASSERT_TRUE(isnan(anomalia_score(&modelo, NULL)));
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        f[i] = 99.0f;
    }
    anomalia_extrair_features(NULL, f);
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, f[i]);
    }
    anomalia_extrair_features(&m, NULL);
}

static void test_modelo_embarcado_coerente(void)
{
    const anomalia_modelo_t *emb = anomalia_modelo_embarcado();
    if (emb == NULL) {
        return;
    }
    TEST_ASSERT_TRUE(emb->limiar > 0.0f);
    for (size_t i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        TEST_ASSERT_TRUE(emb->escala[i] > 0.0f);
        TEST_ASSERT_TRUE(emb->precisao[i][i] > 0.0f);
    }
}

void rodar_testes_anomaly_detector(void)
{
    RUN_TEST(test_features_ordem_e_transformacao);
    RUN_TEST(test_features_entradas_invalidas_saem_finitas);
    RUN_TEST(test_score_zero_na_media);
    RUN_TEST(test_score_identidade_e_distancia_euclidiana);
    RUN_TEST(test_score_escala_padroniza);
    RUN_TEST(test_score_escala_invalida_vira_um);
    RUN_TEST(test_score_precisao_cheia);
    RUN_TEST(test_limiar_estrito_e_invalidos);
    RUN_TEST(test_defensivos);
    RUN_TEST(test_modelo_embarcado_coerente);
}
