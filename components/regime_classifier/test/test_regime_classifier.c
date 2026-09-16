/*
 * Testes Unity do regime_classifier — corpus on-target (test_app). Modelo sintético
 * com média global 0, escala 1 e precisão identidade: a distância vira euclidiana no
 * espaço das features e o centro de cada regime é a feature de uma janela conhecida.
 */
#include "unity.h"

#include "regime_classifier.h"

#include <math.h>
#include <string.h>

static regime_modelo_t modelo;
static metricas_t janela[3];

static void preparar_janela(metricas_t *m, float rms)
{
    memset(m, 0, sizeof(*m));
    for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
        m->rms[e] = rms;
        m->harmonica_1x[e] = rms / 10.0f;
        m->kurtosis[e] = 0.0f;
        m->thd[e] = 0.5f;
    }
}

/* Três regimes (0 parado, 1 e 3) com centros em janelas de RMS 0,01 / 0,2 / 0,8. */
static void preparar_modelo(void)
{
    memset(&modelo, 0, sizeof(modelo));
    modelo.n_classes = 3;
    const int valores[3] = {0, 1, 3};
    const float rms[3] = {0.01f, 0.2f, 0.8f};
    for (int i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        modelo.escala[i] = 1.0f;
    }
    for (int k = 0; k < 3; ++k) {
        modelo.valor[k] = valores[k];
        preparar_janela(&janela[k], rms[k]);
        anomalia_extrair_features(&janela[k], modelo.media[k]);
        for (int i = 0; i < ANOMALIA_N_FEATURES; ++i) {
            modelo.precisao[k][i][i] = 1.0f;
        }
        modelo.limiar_atencao[k] = 2.0f;
        modelo.limiar_critico[k] = 4.0f;
    }
}

static void test_janela_no_centro_do_regime(void)
{
    preparar_modelo();
    regime_resultado_t r;
    for (int k = 0; k < 3; ++k) {
        regime_classificar(&modelo, &janela[k], &r);
        TEST_ASSERT_EQUAL_INT(modelo.valor[k], r.regime);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, r.distancia);
        TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, r.estado);
    }
}

static void test_janela_intermediaria_vai_ao_regime_mais_proximo(void)
{
    preparar_modelo();
    metricas_t m;
    preparar_janela(&m, 0.25f); /* log(0,25) está mais perto de log(0,2) que de log(0,8) */
    regime_resultado_t r;
    regime_classificar(&modelo, &m, &r);
    TEST_ASSERT_EQUAL_INT(1, r.regime);
    TEST_ASSERT_TRUE(r.distancia > 0.0f);
}

static void test_log_det_desempata_a_favor_do_regime_mais_concentrado(void)
{
    preparar_modelo();
    /* Mesmo centro para os regimes 1 e 3: vence o de menor log|Σ|. */
    memcpy(modelo.media[2], modelo.media[1], sizeof(modelo.media[1]));
    modelo.log_det[1] = 5.0f;
    modelo.log_det[2] = -5.0f;
    regime_resultado_t r;
    regime_classificar(&modelo, &janela[1], &r);
    TEST_ASSERT_EQUAL_INT(3, r.regime);
}

static void test_distancia_define_atencao_e_critico(void)
{
    preparar_modelo();
    modelo.n_classes = 1; /* só o regime parado: toda janela é julgada contra ele */
    regime_resultado_t r;

    modelo.limiar_atencao[0] = 100.0f;
    modelo.limiar_critico[0] = 200.0f;
    regime_classificar(&modelo, &janela[1], &r);
    const float d = r.distancia;
    TEST_ASSERT_TRUE(d > 0.0f);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, r.estado);

    modelo.limiar_atencao[0] = d * 0.5f;
    modelo.limiar_critico[0] = d * 2.0f;
    regime_classificar(&modelo, &janela[1], &r);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_AMARELO, r.estado);

    modelo.limiar_critico[0] = d * 0.9f;
    regime_classificar(&modelo, &janela[1], &r);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, r.estado);
}

static void test_falha_conhecida_e_critico_mesmo_no_centro(void)
{
    preparar_modelo();
    modelo.valor[2] = REGIME_FALHA;
    modelo.falha[2] = true;
    regime_resultado_t r;
    regime_classificar(&modelo, &janela[2], &r);
    TEST_ASSERT_EQUAL_INT(REGIME_FALHA, r.regime);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, r.distancia);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, r.estado);

    regime_classificar(&modelo, &janela[1], &r); /* classe saudável segue verde */
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, r.estado);
}

static void test_sem_modelo_ou_entrada_invalida(void)
{
    preparar_modelo();
    regime_resultado_t r;
    regime_classificar(NULL, &janela[0], &r);
    TEST_ASSERT_EQUAL_INT(REGIME_DESCONHECIDO, r.regime);
    TEST_ASSERT_TRUE(isnan(r.distancia));
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, r.estado);

    regime_classificar(&modelo, NULL, &r);
    TEST_ASSERT_EQUAL_INT(REGIME_DESCONHECIDO, r.regime);

    modelo.n_classes = REGIME_MAX_CLASSES + 1;
    regime_classificar(&modelo, &janela[0], &r);
    TEST_ASSERT_EQUAL_INT(REGIME_DESCONHECIDO, r.regime);

    regime_classificar(&modelo, &janela[0], NULL); /* não pode travar */
}

static void test_filtro_adota_a_primeira_janela(void)
{
    regime_filtro_t filtro;
    regime_filtro_iniciar(&filtro);
    TEST_ASSERT_EQUAL_INT(2, regime_filtro_atualizar(&filtro, 2));
    TEST_ASSERT_EQUAL_INT(2, regime_filtro_atualizar(&filtro, 1)); /* sem maioria: mantém */
}

static void test_filtro_troca_so_com_maioria_sustentada(void)
{
    regime_filtro_t filtro;
    regime_filtro_iniciar(&filtro);
    for (int i = 0; i < REGIME_FILTRO_JANELAS; ++i) {
        regime_filtro_atualizar(&filtro, 3);
    }
    for (int i = 1; i < REGIME_FILTRO_MINIMO; ++i) {
        TEST_ASSERT_EQUAL_INT(3, regime_filtro_atualizar(&filtro, 0));
    }
    TEST_ASSERT_EQUAL_INT(0, regime_filtro_atualizar(&filtro, 0)); /* REGIME_FILTRO_MINIMO janelas: troca */
}

static void test_filtro_ignora_alternancia(void)
{
    /* Caso real da bancada: velocidade 3 alternando com a 2 janela a janela. */
    regime_filtro_t filtro;
    regime_filtro_iniciar(&filtro);
    regime_filtro_atualizar(&filtro, 3);
    for (int i = 0; i < 80; ++i) {
        /* 2 e 3 intercalados: nenhum dos dois chega a REGIME_FILTRO_MINIMO em 9 janelas. */
        TEST_ASSERT_EQUAL_INT(3, regime_filtro_atualizar(&filtro, (i % 2 == 0) ? 2 : 3));
    }
}

static void test_modelo_embarcado_coerente(void)
{
    const regime_modelo_t *emb = regime_modelo_embarcado();
    if (emb == NULL) {
        TEST_PASS_MESSAGE("sem modelo de regime embarcado (placeholder)");
        return;
    }
    TEST_ASSERT_TRUE(emb->n_classes >= 2 && emb->n_classes <= REGIME_MAX_CLASSES);
    for (int k = 0; k < emb->n_classes; ++k) {
        TEST_ASSERT_TRUE(emb->limiar_atencao[k] > 0.0f);
        TEST_ASSERT_TRUE(emb->limiar_critico[k] >= emb->limiar_atencao[k]);
    }
    for (int i = 0; i < ANOMALIA_N_FEATURES; ++i) {
        TEST_ASSERT_TRUE(emb->escala[i] > 0.0f);
    }
}

void rodar_testes_regime_classifier(void)
{
    RUN_TEST(test_janela_no_centro_do_regime);
    RUN_TEST(test_janela_intermediaria_vai_ao_regime_mais_proximo);
    RUN_TEST(test_log_det_desempata_a_favor_do_regime_mais_concentrado);
    RUN_TEST(test_distancia_define_atencao_e_critico);
    RUN_TEST(test_falha_conhecida_e_critico_mesmo_no_centro);
    RUN_TEST(test_sem_modelo_ou_entrada_invalida);
    RUN_TEST(test_filtro_adota_a_primeira_janela);
    RUN_TEST(test_filtro_troca_so_com_maioria_sustentada);
    RUN_TEST(test_filtro_ignora_alternancia);
    RUN_TEST(test_modelo_embarcado_coerente);
}
