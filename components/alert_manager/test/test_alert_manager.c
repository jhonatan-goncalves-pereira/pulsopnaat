/*
 * Testes Unity do componente alert_manager — corpus executado ON-TARGET
 * (app de teste `test_app`), mesmo padrão dos demais test_*.c:
 *
 *   idf.py -C test_app build flash monitor
 *   → resumo Unity no serial (N Tests, 0 Failures, OK)
 *
 * Cobre o núcleo PURO (seams 1 e 2 da SPEC — classificação e máquina de
 * estados); a face on-device (alerta_servico: fila, LED, buzzer) é integração
 * validada por observação no dispositivo.
 *
 * Classificação 3σ/6σ (baseline sintético: média 10, σ 2 → atenção > 16,
 * crítica > 22):
 *     1. valor no/nos limiares exatos → normal (estrito: "ultrapassar").
 *     2. logo acima de 3σ → atenção; logo acima de 6σ → crítica.
 *     3. entre 3σ e 6σ → atenção; abaixo → normal.
 *     4. σ = 0 → limiares colapsam na média (leitura literal; acima da
 *        média já é crítica — crítica avaliada antes).
 *     5. Defensivos: baseline NULL/inválido, métrica/eixo fora de faixa.
 *   Votação por eixo (6 métricas):
 *     6. 0 alertas → verde; 1–2 atenções → amarelo; 3+ atenções → vermelho.
 *     7. 1 crítica (resto normal) → vermelho; crítica+atenções → vermelho.
 *     8. Eixos independentes: alerta em Y não vaza para X/Z.
 *   Pior eixo:
 *     9. X verde, Y amarelo, Z verde → amarelo; qualquer vermelho → vermelho.
 *    10. Todos verde → verde (equipamento saudável).
 *   Máquina de estados (transitar — sequências da SPEC):
 *    11. BOOT→CALIBRANDO (comando); BOOT→MONITORANDO (baseline da NVS).
 *    12. CALIBRANDO→MONITORANDO (baseline pronto); WIFI_* ignorados na
 *        calibração; comando repetido ignorado.
 *    13. MONITORANDO⇄CONTINGENCIA (WIFI_CAIR/WIFI_RESTAURADO).
 *    14. Recalibração: MONITORANDO+comando→CALIBRANDO; comando ignorado em
 *        CONTINGENCIA.
 *    15. Sequência canônica completa BOOT→CALIBRANDO→MONITORANDO→
 *        CONTINGENCIA→MONITORANDO.
 *    16. Eventos redundantes são idempotentes (estado não oscila).
 *   Sinalização:
 *    17. BOOT → LED off; CALIBRANDO → calibrando, buzzer SEMPRE off.
 *    18. MONITORANDO: verde→normal, amarelo→atenção (buzzer off),
 *        vermelho→crítico + buzzer ON (RF06).
 *    19. CONTINGENCIA: sem conexão; buzzer só se vermelho.
 *    20. Defensivo: sinal NULL → no-op.
 */
#include "unity.h"

#include "alert_manager.h"

#include <math.h>
#include <string.h>

/* Baseline sintético: média 10,0 e σ 2,0 em TODAS as células →
 * limiar de atenção 16,0 (10+3·2), crítico 22,0 (10+6·2). */
#define MEDIA_BASE 10.0f
#define SIGMA_BASE 2.0f

static baseline_t base;
static metricas_t m;

/* setUp()/tearDown() NÃO são definidos aqui — vivem em único arquivo no
 * runner (hoje test_signal_processing.c); cada teste chama preparar(). */
static void preparar(void)
{
    for (int i = 0; i < METRICA_NUM; ++i) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            base.media[i][e] = MEDIA_BASE;
            base.desvio_padrao[i][e] = SIGMA_BASE;
        }
    }
    memset(&m, 0, sizeof(m));
}

/* Define uma célula [métrica][eixo] de metricas_t (layout contíguo de
 * float[3], garantido pelo _Static_assert de signal_processing.c). */
static void celula(metrica_id_t met, eixo_t eixo, float valor)
{
    ((float *)&m.rms)[met * JANELA_NUM_EIXOS + eixo] = valor;
}

/* Deixa TODAS as células num estado (normal p/ base de 10±2 → 10,0). */
static void janela_normal(void)
{
    for (int i = 0; i < METRICA_NUM; ++i) {
        for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
            celula((metrica_id_t)i, (eixo_t)e, MEDIA_BASE);
        }
    }
}

/* ===================== Classificação por métrica ======================== */

/* 1. Limiares exatos (estrito: tem que ULTRAPASSAR):
 *      == média+3σ → não ultrapassou 3σ → NORMAL;
 *      == média+6σ → ultrapassou 3σ (mas não 6σ) → ATENÇÃO;
 *      >  média+6σ → CRÍTICA (coberto no teste 2). */
void test_metrica_nos_limiares_e_normal(void)
{
    preparar();
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE));
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE + 3.0f * SIGMA_BASE));
    TEST_ASSERT_EQUAL(CLASSIF_ATENCAO,
                      alerta_classificar_metrica(&base, METRICA_THD, EIXO_Z,
                                                 MEDIA_BASE + 6.0f * SIGMA_BASE));
}

/* 2. Acima de 3σ → atenção; acima de 6σ → crítica. */
void test_metrica_acima_dos_limiares(void)
{
    preparar();
    TEST_ASSERT_EQUAL(CLASSIF_ATENCAO,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE + 3.0f * SIGMA_BASE +
                                                     0.01f));
    TEST_ASSERT_EQUAL(CLASSIF_CRITICA,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE + 6.0f * SIGMA_BASE +
                                                     0.01f));
    /* Muito acima de 6σ segue crítica (nunca rebaixa para atenção). */
    TEST_ASSERT_EQUAL(CLASSIF_CRITICA,
                      alerta_classificar_metrica(&base, METRICA_KURTOSIS,
                                                 EIXO_Y, MEDIA_BASE * 100.0f));
}

/* 3. Faixas: abaixo → normal; entre 3σ e 6σ → atenção. */
void test_metrica_faixas_intermediarias(void)
{
    preparar();
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_HARMONICA_1X,
                                                 EIXO_Y, MEDIA_BASE - 1.0f));
    TEST_ASSERT_EQUAL(CLASSIF_ATENCAO,
                      alerta_classificar_metrica(&base, METRICA_HARMONICA_1X,
                                                 EIXO_Y,
                                                 MEDIA_BASE + 4.5f * SIGMA_BASE));
}

/* 4. σ = 0: limiares colapsam na média (leitura literal de média+3σ/6σ). */
void test_metrica_sigma_zero(void)
{
    preparar();
    base.desvio_padrao[METRICA_RMS][EIXO_X] = 0.0f;
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE));
    /* Crítica é avaliada primeiro: acima da média (6σ = 0) já é crítica. */
    TEST_ASSERT_EQUAL(CLASSIF_CRITICA,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 MEDIA_BASE + 0.01f));
}

/* 5. Defensivos: sem baseline válido não há alerta. */
void test_metrica_defensivos(void)
{
    preparar();
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(NULL, METRICA_RMS, EIXO_X,
                                                 1e9f));
    base.desvio_padrao[METRICA_RMS][EIXO_X] = -1.0f; /* baseline inválido */
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_RMS, EIXO_X,
                                                 1e9f));
    base.desvio_padrao[METRICA_RMS][EIXO_X] = SIGMA_BASE;
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, (metrica_id_t)99,
                                                 EIXO_X, 1e9f));
    TEST_ASSERT_EQUAL(CLASSIF_NORMAL,
                      alerta_classificar_metrica(&base, METRICA_RMS,
                                                 (eixo_t)7, 1e9f));
}

/* ========================= Votação / pior eixo ========================== */

/* 6. Votação por contagem de atenções: 0→verde, 1–2→amarelo, 3+→vermelho. */
void test_votacao_por_atencoes(void)
{
    preparar();
    janela_normal();

    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE,
                      alerta_classificar_eixo(&base, &m, EIXO_X));

    celula(METRICA_RMS, EIXO_X, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_AMARELO,
                      alerta_classificar_eixo(&base, &m, EIXO_X));

    celula(METRICA_THD, EIXO_X, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_AMARELO,
                      alerta_classificar_eixo(&base, &m, EIXO_X));

    celula(METRICA_KURTOSIS, EIXO_X, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, /* 3 atenções */
                      alerta_classificar_eixo(&base, &m, EIXO_X));

    celula(METRICA_HARMONICA_1X, EIXO_X, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, /* 4 atenções */
                      alerta_classificar_eixo(&base, &m, EIXO_X));
}

/* 7. Uma crítica basta para vermelho (resto normal ou atenção). */
void test_votacao_com_critica(void)
{
    preparar();
    janela_normal();
    celula(METRICA_HARMONICA_2X, EIXO_Y, MEDIA_BASE + 7.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO,
                      alerta_classificar_eixo(&base, &m, EIXO_Y));

    janela_normal();
    celula(METRICA_BANDA_3X_5X, EIXO_Y, MEDIA_BASE + 7.0f * SIGMA_BASE);
    celula(METRICA_RMS, EIXO_Y, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO,
                      alerta_classificar_eixo(&base, &m, EIXO_Y));
}

/* 8. Eixos independentes: alerta em Y não vaza para X/Z (ISO 20816-3). */
void test_eixos_independentes(void)
{
    preparar();
    janela_normal();
    celula(METRICA_RMS, EIXO_Y, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE,
                      alerta_classificar_eixo(&base, &m, EIXO_X));
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_AMARELO,
                      alerta_classificar_eixo(&base, &m, EIXO_Y));
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE,
                      alerta_classificar_eixo(&base, &m, EIXO_Z));
}

/* 9/10. Pior eixo classifica o equipamento. */
void test_pior_eixo(void)
{
    preparar();
    janela_normal();
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, alerta_classificar_janela(&base, &m));

    celula(METRICA_RMS, EIXO_Y, MEDIA_BASE + 4.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_AMARELO, alerta_classificar_janela(&base, &m));

    celula(METRICA_KURTOSIS, EIXO_Z, MEDIA_BASE + 7.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, /* Z crítico domina */
                      alerta_classificar_janela(&base, &m));
}

/* 9b. Pior eixo: eixo crítico domina mesmo com outros em atenção. */
void test_pior_eixo_critico_domina(void)
{
    preparar();
    janela_normal();
    celula(METRICA_RMS, EIXO_X, MEDIA_BASE + 4.0f * SIGMA_BASE);
    celula(METRICA_THD, EIXO_Z, MEDIA_BASE + 7.0f * SIGMA_BASE);
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERMELHO, alerta_classificar_janela(&base, &m));
}

/* 10b. Sem baseline válido → verde (nunca classifica sem baseline). */
void test_classificacao_sem_baseline(void)
{
    preparar();
    janela_normal();
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE, alerta_classificar_janela(NULL, &m));
    TEST_ASSERT_EQUAL(ESTADO_EQUIP_VERDE,
                      alerta_classificar_janela(&base, NULL));
}

/* ========================= Máquina de estados =========================== */

/* 11. BOOT: comando → CALIBRANDO; baseline da NVS → MONITORANDO. */
void test_maquina_boot(void)
{
    preparar();
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO,
                      transitar(ESTADO_MAQ_BOOT, EVENTO_INICIAR_CALIBRACAO));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO,
                      transitar(ESTADO_MAQ_BOOT, EVENTO_BASELINE_DISPONIVEL));
    /* Eventos de rede antes do fim do boot não movem a máquina. */
    TEST_ASSERT_EQUAL(ESTADO_MAQ_BOOT, transitar(ESTADO_MAQ_BOOT, EVENTO_WIFI_CAIR));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_BOOT,
                      transitar(ESTADO_MAQ_BOOT, EVENTO_WIFI_RESTAURADO));
}

/* 12. CALIBRANDO: só o baseline pronto move; rede e comando repetido não. */
void test_maquina_calibrando(void)
{
    preparar();
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO,
                      transitar(ESTADO_MAQ_CALIBRANDO, EVENTO_BASELINE_DISPONIVEL));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO,
                      transitar(ESTADO_MAQ_CALIBRANDO, EVENTO_WIFI_CAIR));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO,
                      transitar(ESTADO_MAQ_CALIBRANDO, EVENTO_WIFI_RESTAURADO));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO,
                      transitar(ESTADO_MAQ_CALIBRANDO, EVENTO_INICIAR_CALIBRACAO));
}

/* 13. MONITORANDO ⇄ CONTINGENCIA. */
void test_maquina_monitorando_contingencia(void)
{
    preparar();
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CONTINGENCIA,
                      transitar(ESTADO_MAQ_MONITORANDO, EVENTO_WIFI_CAIR));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO,
                      transitar(ESTADO_MAQ_CONTINGENCIA, EVENTO_WIFI_RESTAURADO));
    /* Eventos não definidos mantêm o estado. */
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO,
                      transitar(ESTADO_MAQ_MONITORANDO, EVENTO_BASELINE_DISPONIVEL));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CONTINGENCIA,
                      transitar(ESTADO_MAQ_CONTINGENCIA, EVENTO_WIFI_CAIR));
}

/* 14. Recalibração comandada; comando ignorado na contingência. */
void test_maquina_recalibracao(void)
{
    preparar();
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO,
                      transitar(ESTADO_MAQ_MONITORANDO, EVENTO_INICIAR_CALIBRACAO));
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CONTINGENCIA,
                      transitar(ESTADO_MAQ_CONTINGENCIA, EVENTO_INICIAR_CALIBRACAO));
}

/* 15. Sequência canônica completa (SPEC). */
void test_maquina_sequencia_canonica(void)
{
    preparar();
    estado_maquina_t s = ESTADO_MAQ_BOOT;
    s = transitar(s, EVENTO_INICIAR_CALIBRACAO);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CALIBRANDO, s);
    s = transitar(s, EVENTO_BASELINE_DISPONIVEL);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO, s);
    s = transitar(s, EVENTO_WIFI_CAIR);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_CONTINGENCIA, s);
    s = transitar(s, EVENTO_WIFI_RESTAURADO);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO, s);
}

/* 16. Reboot com baseline persistido: BOOT→MONITORANDO direto (sem passar
 *     por CALIBRANDO), e a máquina não volta a BOOT sozinha. */
void test_maquina_reboot_com_baseline(void)
{
    preparar();
    estado_maquina_t s = transitar(ESTADO_MAQ_BOOT, EVENTO_BASELINE_DISPONIVEL);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO, s);
    s = transitar(s, EVENTO_WIFI_CAIR);
    s = transitar(s, EVENTO_WIFI_RESTAURADO);
    TEST_ASSERT_EQUAL(ESTADO_MAQ_MONITORANDO, s);
}

/* ============================== Sinalização ============================== */

/* 17. BOOT → LED off; CALIBRANDO → indica calibrando e NUNCA buzzer. */
void test_sinalizacao_boot_calibrando(void)
{
    preparar();
    sinalizacao_t s = {0};
    alerta_sinalizar(ESTADO_MAQ_BOOT, ESTADO_EQUIP_VERMELHO, &s);
    TEST_ASSERT_EQUAL(LED_IND_OFF, s.led);
    TEST_ASSERT_FALSE(s.buzzer);

    alerta_sinalizar(ESTADO_MAQ_CALIBRANDO, ESTADO_EQUIP_VERDE, &s);
    TEST_ASSERT_EQUAL(LED_IND_CALIBRANDO, s.led);
    TEST_ASSERT_FALSE(s.buzzer);
    alerta_sinalizar(ESTADO_MAQ_CALIBRANDO, ESTADO_EQUIP_VERMELHO, &s);
    TEST_ASSERT_FALSE(s.buzzer); /* sem classificação durante a coleta */
}

/* 18. MONITORANDO reflete o estado do equipamento; buzzer só no vermelho. */
void test_sinalizacao_monitorando(void)
{
    preparar();
    sinalizacao_t s = {0};

    alerta_sinalizar(ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_VERDE, &s);
    TEST_ASSERT_EQUAL(LED_IND_NORMAL, s.led);
    TEST_ASSERT_FALSE(s.buzzer);

    alerta_sinalizar(ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_AMARELO, &s);
    TEST_ASSERT_EQUAL(LED_IND_ATENCAO, s.led);
    TEST_ASSERT_FALSE(s.buzzer);

    alerta_sinalizar(ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_VERMELHO, &s);
    TEST_ASSERT_EQUAL(LED_IND_CRITICO, s.led);
    TEST_ASSERT_TRUE(s.buzzer); /* RF06: buzzer APENAS em crítico */
}

/* 19. CONTINGENCIA → sem conexão; buzzer mantido só se segue vermelho. */
void test_sinalizacao_contingencia(void)
{
    preparar();
    sinalizacao_t s = {0};

    alerta_sinalizar(ESTADO_MAQ_CONTINGENCIA, ESTADO_EQUIP_VERDE, &s);
    TEST_ASSERT_EQUAL(LED_IND_SEM_CONEXAO, s.led);
    TEST_ASSERT_FALSE(s.buzzer);

    alerta_sinalizar(ESTADO_MAQ_CONTINGENCIA, ESTADO_EQUIP_AMARELO, &s);
    TEST_ASSERT_FALSE(s.buzzer);

    alerta_sinalizar(ESTADO_MAQ_CONTINGENCIA, ESTADO_EQUIP_VERMELHO, &s);
    TEST_ASSERT_EQUAL(LED_IND_SEM_CONEXAO, s.led);
    TEST_ASSERT_TRUE(s.buzzer);
}

/* 20. Defensivo: sinal NULL → no-op (sem crash). */
void test_sinalizacao_defensivos(void)
{
    preparar();
    alerta_sinalizar(ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_VERMELHO, NULL);
    TEST_ASSERT_TRUE(true); /* chegou aqui sem falhar */
}

/* Registro dos testes — chamado pelo runner entre UNITY_BEGIN/UNITY_END. */
void rodar_testes_alert_manager(void)
{
    /* Classificação por métrica */
    RUN_TEST(test_metrica_nos_limiares_e_normal);
    RUN_TEST(test_metrica_acima_dos_limiares);
    RUN_TEST(test_metrica_faixas_intermediarias);
    RUN_TEST(test_metrica_sigma_zero);
    RUN_TEST(test_metrica_defensivos);
    /* Votação / pior eixo */
    RUN_TEST(test_votacao_por_atencoes);
    RUN_TEST(test_votacao_com_critica);
    RUN_TEST(test_eixos_independentes);
    RUN_TEST(test_pior_eixo);
    RUN_TEST(test_pior_eixo_critico_domina);
    RUN_TEST(test_classificacao_sem_baseline);
    /* Máquina de estados */
    RUN_TEST(test_maquina_boot);
    RUN_TEST(test_maquina_calibrando);
    RUN_TEST(test_maquina_monitorando_contingencia);
    RUN_TEST(test_maquina_recalibracao);
    RUN_TEST(test_maquina_sequencia_canonica);
    RUN_TEST(test_maquina_reboot_com_baseline);
    /* Sinalização */
    RUN_TEST(test_sinalizacao_boot_calibrando);
    RUN_TEST(test_sinalizacao_monitorando);
    RUN_TEST(test_sinalizacao_contingencia);
    RUN_TEST(test_sinalizacao_defensivos);
}
