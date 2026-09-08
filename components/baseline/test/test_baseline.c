/*
 * Testes Unity do baseline — corpus executado ON-TARGET (test_app), mesmo
 * padrão de test_signal_processing.c:
 *   idf.py -C test_app build flash monitor → "N Tests, 0 Failures, OK"
 *
 * Componente PURO (sem ESP-IDF/I/O): cobre a lógica (Welford, validação,
 * serialização); a NVS é camada fina validada on-device na integração.
 * Índice dos casos: lista de RUN_TEST no fim.
 *
 * Valores exatos escolhidos para não depender de tolerância ({2,4} → média
 * 3, σ 1; 15+15 mantém exatidão binária).
 */
#include "unity.h"

#include "baseline.h"

#include <math.h>
#include <string.h>

/* Acumulador e baseline FORA da pilha (padrão dos demais testes: estáticos,
 * reescritos a cada uso). setUp()/tearDown() NÃO são definidos aqui — vivem
 * em único arquivo no runner; cada teste chama reiniciar(). */
static baseline_calibracao_t cal;
static baseline_t b;

static void reiniciar(void)
{
    memset(&cal, 0, sizeof(cal));
    memset(&b, 0, sizeof(b));
}

/* metricas_t com TODOS os campos iguais a `v` (uma "janela" sintética). */
static void metrica_constante(metricas_t *m, float v)
{
    float *p = &m->rms[0];
    for (size_t i = 0; i < sizeof(metricas_t) / sizeof(float); ++i) {
        p[i] = v;
    }
}

/* metricas_t com valor `a` em TODAS as células exceto uma (métrica/eixo)
 * que recebe `b_` — para testar isolamento por célula. */
static void metrica_com_celula(metricas_t *m, float a, metrica_id_t met,
                               eixo_t eixo, float b_)
{
    metrica_constante(m, a);
    float v[METRICA_NUM][JANELA_NUM_EIXOS];
    metricas_para_vetor(m, v);
    v[met][eixo] = b_;
    float *p = &m->rms[0];
    for (int i = 0; i < METRICA_NUM; ++i) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            p[i * JANELA_NUM_EIXOS + e] = v[i][e];
        }
    }
}

/* ========================= Calibração (Welford) ========================= */

/* 1. 30 janelas idênticas (todas as células = 2,0) → média 2,0, σ 0. */
void test_calibracao_janelas_identicas_media_sigma(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 2.0f);
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        const bool completa = baseline_calibracao_adicionar(&cal, &m);
        if (i < BASELINE_N_JANELAS - 1) {
            TEST_ASSERT_FALSE_MESSAGE(completa, "completa antes da 30a janela");
        } else {
            TEST_ASSERT_TRUE(completa); /* a 30ª completa a coleta */
        }
    }
    TEST_ASSERT_TRUE(baseline_calibracao_completa(&cal));
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, b.media[METRICA_RMS][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, b.media[METRICA_THD][EIXO_Z]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.desvio_padrao[METRICA_RMS][EIXO_X]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, b.desvio_padrao[METRICA_THD][EIXO_Z]);
}

/* 2. 30 janelas alternando 2,0 e 4,0 (15 de cada) → média 3,0, σ 1,0
 *     (exatos: variância populacional de {2×15, 4×15} = 1). */
void test_calibracao_janelas_alternadas_media_sigma(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        metrica_constante(&m, (i % 2 == 0) ? 2.0f : 4.0f);
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, b.media[METRICA_RMS][EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, b.desvio_padrao[METRICA_RMS][EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, b.media[METRICA_KURTOSIS][EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, b.desvio_padrao[METRICA_KURTOSIS][EIXO_Y]);
}

/* 3. Coleta parcial não extrai; ao completar, extrai. */
void test_calibracao_parcial_nao_extrai(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 1.0f);
    for (int i = 0; i < BASELINE_N_JANELAS - 1; ++i) {
        TEST_ASSERT_FALSE(baseline_calibracao_adicionar(&cal, &m));
    }
    TEST_ASSERT_FALSE(baseline_calibracao_completa(&cal));
    TEST_ASSERT_FALSE(baseline_calibracao_extrair(&cal, &b)); /* incompleta */
    TEST_ASSERT_TRUE(baseline_calibracao_adicionar(&cal, &m)); /* 30a */
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, b.media[METRICA_RMS][EIXO_Z]);
}

/* 4. Janelas além da 30ª são ignoradas — a média não muda. */
void test_calibracao_excedentes_ignoradas(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 1.0f);
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        baseline_calibracao_adicionar(&cal, &m);
    }
    metrica_constante(&m, 1000.0f); /* janela 31 — não pode contaminar */
    TEST_ASSERT_TRUE(baseline_calibracao_adicionar(&cal, &m));
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, b.media[METRICA_RMS][EIXO_X]);
}

/* 5. Defensivos: NULL não conta janela e não crasha. */
void test_calibracao_defensivos(void)
{
    reiniciar();
    metricas_t m;
    metrica_constante(&m, 1.0f);
    baseline_calibracao_iniciar(NULL);           /* no-op */
    TEST_ASSERT_FALSE(baseline_calibracao_adicionar(NULL, &m));
    TEST_ASSERT_FALSE(baseline_calibracao_adicionar(&cal, NULL));
    TEST_ASSERT_FALSE(baseline_calibracao_completa(NULL));
    TEST_ASSERT_FALSE(baseline_calibracao_extrair(NULL, &b));
    TEST_ASSERT_FALSE(baseline_calibracao_extrair(&cal, NULL));
    baseline_calibracao_iniciar(&cal);
    TEST_ASSERT_EQUAL_UINT32(0, cal.n);
}

/* 6. Isolamento por célula: só a métrica/eixo alimentado difere. */
void test_calibracao_isolamento_por_celula(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        metrica_com_celula(&m, 1.0f, METRICA_HARMONICA_2X, EIXO_Y, 5.0f);
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, b.media[METRICA_HARMONICA_2X][EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, b.media[METRICA_HARMONICA_2X][EIXO_X]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, b.media[METRICA_RMS][EIXO_Y]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, b.media[METRICA_THD][EIXO_Z]);
}

/* ============================== Validação =============================== */

/* 7. baseline_valido: rejeita NaN e σ negativo; aceita o caso normal. */
void test_baseline_valido(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 2.0f);
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));
    TEST_ASSERT_TRUE(baseline_valido(&b));

    b.media[METRICA_RMS][EIXO_X] = NAN;
    TEST_ASSERT_FALSE(baseline_valido(&b));
    b.media[METRICA_RMS][EIXO_X] = INFINITY;
    TEST_ASSERT_FALSE(baseline_valido(&b));
    b.media[METRICA_RMS][EIXO_X] = 2.0f;
    b.desvio_padrao[METRICA_THD][EIXO_Z] = -0.5f;
    TEST_ASSERT_FALSE(baseline_valido(&b));
    TEST_ASSERT_FALSE(baseline_valido(NULL));
}

/* ==================== Serialização (registro NVS) ====================== */

/* 8. Roundtrip empacotar → desempacotar preserva todas as células. */
void test_empacotar_desempacotar_roundtrip(void)
{
    reiniciar();
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        metrica_com_celula(&m, (float)i, METRICA_RMS, EIXO_X, (float)(30 - i));
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &b));

    uint8_t pacote[BASELINE_TAM_PACOTE];
    baseline_empacotar(&b, pacote);
    baseline_t lido;
    TEST_ASSERT_TRUE(baseline_desempacotar(pacote, &lido));
    TEST_ASSERT_EQUAL_MEMORY(b.media, lido.media, sizeof(b.media));
    TEST_ASSERT_EQUAL_MEMORY(b.desvio_padrao, lido.desvio_padrao,
                             sizeof(b.desvio_padrao));
}

/* 9. Um byte qualquer corrompido → CRC reprova → false (e `out` intocado). */
void test_desempacotar_rejeita_corrompido(void)
{
    reiniciar();
    uint8_t pacote[BASELINE_TAM_PACOTE];
    memset(pacote, 0, sizeof(pacote));
    baseline_t orig;
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 3.0f);
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &orig));
    baseline_empacotar(&orig, pacote);

    pacote[50] ^= 0xFF; /* meia vida: célula média/desvio corrompida */
    baseline_t lido;
    lido.media[0][0] = -99.0f; /* sentinela: contrato garante `out` intocado */
    TEST_ASSERT_FALSE(baseline_desempacotar(pacote, &lido));
    TEST_ASSERT_EQUAL_FLOAT(-99.0f, lido.media[0][0]);
}

/* 10. Mágica e versão erradas → false (registro de outro layout/entidade). */
void test_desempacotar_rejeita_magica_versao(void)
{
    reiniciar();
    uint8_t pacote[BASELINE_TAM_PACOTE];
    baseline_t orig;
    baseline_calibracao_iniciar(&cal);
    metricas_t m;
    metrica_constante(&m, 1.0f);
    for (int i = 0; i < BASELINE_N_JANELAS; ++i) {
        baseline_calibracao_adicionar(&cal, &m);
    }
    TEST_ASSERT_TRUE(baseline_calibracao_extrair(&cal, &orig));
    baseline_empacotar(&orig, pacote);

    baseline_t lido;
    pacote[0] = 'X';
    TEST_ASSERT_FALSE(baseline_desempacotar(pacote, &lido));
    baseline_empacotar(&orig, pacote);
    pacote[4] = 99; /* versão futura/antiga */
    TEST_ASSERT_FALSE(baseline_desempacotar(pacote, &lido));
}

/* 11. NaN dentro do registro (CRC válida se recalculada... aqui o NaN é
 *     gravado e o CRC acompanha) → baseline_valido reprova → false. */
void test_desempacotar_rejeita_conteudo_invalido(void)
{
    reiniciar();
    baseline_t sujo;
    memset(&sujo, 0, sizeof(sujo));
    sujo.media[METRICA_KURTOSIS][EIXO_X] = NAN;
    uint8_t pacote[BASELINE_TAM_PACOTE];
    baseline_empacotar(&sujo, pacote); /* CRC cobre o NaN gravado */
    baseline_t lido;
    TEST_ASSERT_FALSE(baseline_desempacotar(pacote, &lido));
}

/* Registro dos testes — chamado pelo runner entre UNITY_BEGIN/UNITY_END. */
void rodar_testes_baseline(void)
{
    RUN_TEST(test_calibracao_janelas_identicas_media_sigma);
    RUN_TEST(test_calibracao_janelas_alternadas_media_sigma);
    RUN_TEST(test_calibracao_parcial_nao_extrai);
    RUN_TEST(test_calibracao_excedentes_ignoradas);
    RUN_TEST(test_calibracao_defensivos);
    RUN_TEST(test_calibracao_isolamento_por_celula);
    RUN_TEST(test_baseline_valido);
    RUN_TEST(test_empacotar_desempacotar_roundtrip);
    RUN_TEST(test_desempacotar_rejeita_corrompido);
    RUN_TEST(test_desempacotar_rejeita_magica_versao);
    RUN_TEST(test_desempacotar_rejeita_conteudo_invalido);
}
