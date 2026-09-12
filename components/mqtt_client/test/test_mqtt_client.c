/*
 * Testes Unity do mqtt_payloads — corpus executado ON-TARGET (test_app):
 *   idf.py -C test_app build flash monitor → "N Tests, 0 Failures, OK"
 *
 * Cobre o módulo PURO (formatação JSON + nomes de estado); o transporte
 * (mqtt_client_publish) e a máquina de estados são integração on-device.
 * setUp()/tearDown() NÃO são definidos aqui — vivem em único arquivo no
 * runner.
 */
#include "unity.h"

#include "mqtt_payloads.h"

#include <string.h>

static metricas_t m;
static char buf[256];

static void preparar(float x, float y, float z)
{
    memset(&m, 0, sizeof(m));
    m.rms[EIXO_X] = x;
    m.rms[EIXO_Y] = y;
    m.rms[EIXO_Z] = z;
}

/* 1. Alerta carrega node, timestamp, estado e RMS por eixo. */
void test_alerta_contem_campos(void)
{
    preparar(1.2345f, 2.3456f, 3.4567f);
    const size_t n = mqtt_formatar_alerta(buf, sizeof(buf), "pulsopnaat-01",
                                          123456789LL, ESTADO_EQUIP_AMARELO, &m);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL(n, strlen(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"node\":\"pulsopnaat-01\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts_us\":123456789"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"estado\":\"amarelo (atenção)\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rms_x\":1.2345"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rms_y\":2.3456"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rms_z\":3.4567"));
}

/* 2. Nomes de estado do protocolo (máquina + equipamento, inclusive "?"). */
void test_nomes_de_estado(void)
{
    TEST_ASSERT_EQUAL_STRING("BOOT", mqtt_nome_estado_maquina(ESTADO_MAQ_BOOT));
    TEST_ASSERT_EQUAL_STRING("CALIBRANDO", mqtt_nome_estado_maquina(ESTADO_MAQ_CALIBRANDO));
    TEST_ASSERT_EQUAL_STRING("MONITORANDO", mqtt_nome_estado_maquina(ESTADO_MAQ_MONITORANDO));
    TEST_ASSERT_EQUAL_STRING("CONTINGENCIA", mqtt_nome_estado_maquina(ESTADO_MAQ_CONTINGENCIA));
    TEST_ASSERT_EQUAL_STRING("?", mqtt_nome_estado_maquina((estado_maquina_t)99));
    TEST_ASSERT_EQUAL_STRING("verde (normal)", mqtt_nome_estado_equipamento(ESTADO_EQUIP_VERDE));
    TEST_ASSERT_EQUAL_STRING("amarelo (atenção)", mqtt_nome_estado_equipamento(ESTADO_EQUIP_AMARELO));
    TEST_ASSERT_EQUAL_STRING("vermelho (crítico)", mqtt_nome_estado_equipamento(ESTADO_EQUIP_VERMELHO));
    TEST_ASSERT_EQUAL_STRING("?", mqtt_nome_estado_equipamento((estado_equipamento_t)99));
}

/* 3. Status carrega maquina/equipamento/baseline/uptime. */
void test_status_contem_campos(void)
{
    const size_t n = mqtt_formatar_status(buf, sizeof(buf), "no-1",
                                          ESTADO_MAQ_MONITORANDO,
                                          ESTADO_EQUIP_VERDE, true, 42LL);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL(n, strlen(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"node\":\"no-1\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"maquina\":\"MONITORANDO\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"equipamento\":\"verde (normal)\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"baseline\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"uptime_s\":42"));
}

/* 4. Baseline ausente serializa false. */
void test_status_baseline_ausente(void)
{
    const size_t n = mqtt_formatar_status(buf, sizeof(buf), "no-1",
                                          ESTADO_MAQ_BOOT,
                                          ESTADO_EQUIP_VERDE, false, 0LL);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"baseline\":false"));
}

/* 5. Defensivos: NULL / len 0 → 0, sem tocar em buffer inválido. */
void test_formatadores_defensivos(void)
{
    preparar(1.0f, 1.0f, 1.0f);
    TEST_ASSERT_EQUAL(0, mqtt_formatar_alerta(NULL, sizeof(buf), "n", 0LL,
                                              ESTADO_EQUIP_VERDE, &m));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_alerta(buf, 0, "n", 0LL,
                                              ESTADO_EQUIP_VERDE, &m));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_alerta(buf, sizeof(buf), NULL, 0LL,
                                              ESTADO_EQUIP_VERDE, &m));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_alerta(buf, sizeof(buf), "n", 0LL,
                                              ESTADO_EQUIP_VERDE, NULL));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_status(NULL, sizeof(buf), "n",
                                              ESTADO_MAQ_BOOT,
                                              ESTADO_EQUIP_VERDE, false, 0LL));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_status(buf, 0, "n", ESTADO_MAQ_BOOT,
                                              ESTADO_EQUIP_VERDE, false, 0LL));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_status(buf, sizeof(buf), NULL,
                                              ESTADO_MAQ_BOOT,
                                              ESTADO_EQUIP_VERDE, false, 0LL));
}

/* 6. Truncação: buffer curto → 0 e string vazia (nunca meio-JSON). */
void test_formatadores_truncacao(void)
{
    char pequeno[16];
    preparar(1.0f, 2.0f, 3.0f);
    TEST_ASSERT_EQUAL(0, mqtt_formatar_alerta(pequeno, sizeof(pequeno), "pulsopnaat-01",
                                              1LL, ESTADO_EQUIP_VERMELHO, &m));
    TEST_ASSERT_EQUAL_STRING("", pequeno);
    TEST_ASSERT_EQUAL(0, mqtt_formatar_status(pequeno, sizeof(pequeno), "pulsopnaat-01",
                                              ESTADO_MAQ_MONITORANDO,
                                              ESTADO_EQUIP_VERMELHO, true, 1LL));
    TEST_ASSERT_EQUAL_STRING("", pequeno);
}

/* Registro dos testes — chamado pelo runner entre UNITY_BEGIN/UNITY_END. */
void rodar_testes_mqtt_client(void)
{
    RUN_TEST(test_alerta_contem_campos);
    RUN_TEST(test_nomes_de_estado);
    RUN_TEST(test_status_contem_campos);
    RUN_TEST(test_status_baseline_ausente);
    RUN_TEST(test_formatadores_defensivos);
    RUN_TEST(test_formatadores_truncacao);
}
