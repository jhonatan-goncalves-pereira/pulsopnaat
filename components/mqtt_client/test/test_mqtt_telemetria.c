/*
 * Testes Unity da telemetria por janela (mqtt_formatar_telemetria) — corpus
 * on-target (test_app). setUp()/tearDown() vivem no runner.
 */
#include "unity.h"

#include "mqtt_payloads.h"

#include <math.h>
#include <string.h>

static metricas_t m;
static char buf[640];

static void preparar(void)
{
    memset(&m, 0, sizeof(m));
    m.rms[EIXO_X] = 1.25f;
    m.harmonica_1x[EIXO_Y] = 0.5f;
    m.kurtosis[EIXO_Z] = -1.5f;
    m.thd[EIXO_Z] = 2.0f;
}

static void test_telemetria_contem_campos(void)
{
    preparar();
    const size_t n = mqtt_formatar_telemetria(buf, sizeof(buf), "pulsopnaat-01", 42LL,
                                              ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_VERMELHO,
                                              &m, 7.5f, true, "falha");
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL(n, strlen(buf));
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('}', buf[n - 1]);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"node\":\"pulsopnaat-01\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ts_us\":42"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"maquina\":\"MONITORANDO\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"equipamento\":\"vermelho (crítico)\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"nivel\":2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rms_x\":1.2500"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"h1x_y\":0.5000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"kurt_z\":-1.5000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"thd_z\":2.0000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"score\":7.5000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"anomalia\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rotulo\":\"falha\""));
}

static void test_telemetria_nivel_por_estado(void)
{
    preparar();
    mqtt_formatar_telemetria(buf, sizeof(buf), "n", 0, ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_VERDE,
                             &m, 1.0f, false, "saudavel");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"nivel\":0"));
    mqtt_formatar_telemetria(buf, sizeof(buf), "n", 0, ESTADO_MAQ_MONITORANDO, ESTADO_EQUIP_AMARELO,
                             &m, 1.0f, false, "saudavel");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"nivel\":1"));
    mqtt_formatar_telemetria(buf, sizeof(buf), "n", 0, ESTADO_MAQ_CALIBRANDO, (estado_equipamento_t)99,
                             &m, 1.0f, false, "saudavel");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"nivel\":-1"));
}

static void test_telemetria_score_nan_vira_null_e_rotulo_padrao(void)
{
    preparar();
    const size_t n = mqtt_formatar_telemetria(buf, sizeof(buf), "n", 0, ESTADO_MAQ_CONTINGENCIA,
                                              ESTADO_EQUIP_VERDE, &m, NAN, false, NULL);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"score\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"anomalia\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rotulo\":\"sem_rotulo\""));
    TEST_ASSERT_NULL(strstr(buf, "nan"));
}

static void test_telemetria_defensivos_e_truncacao(void)
{
    preparar();
    TEST_ASSERT_EQUAL(0, mqtt_formatar_telemetria(NULL, sizeof(buf), "n", 0, ESTADO_MAQ_MONITORANDO,
                                                  ESTADO_EQUIP_VERDE, &m, 1.0f, false, "x"));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_telemetria(buf, 0, "n", 0, ESTADO_MAQ_MONITORANDO,
                                                  ESTADO_EQUIP_VERDE, &m, 1.0f, false, "x"));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_telemetria(buf, sizeof(buf), NULL, 0, ESTADO_MAQ_MONITORANDO,
                                                  ESTADO_EQUIP_VERDE, &m, 1.0f, false, "x"));
    TEST_ASSERT_EQUAL(0, mqtt_formatar_telemetria(buf, sizeof(buf), "n", 0, ESTADO_MAQ_MONITORANDO,
                                                  ESTADO_EQUIP_VERDE, NULL, 1.0f, false, "x"));
    char pequeno[64];
    TEST_ASSERT_EQUAL(0, mqtt_formatar_telemetria(pequeno, sizeof(pequeno), "n", 0, ESTADO_MAQ_MONITORANDO,
                                                  ESTADO_EQUIP_VERDE, &m, 1.0f, false, "x"));
    TEST_ASSERT_EQUAL_CHAR('\0', pequeno[0]);
}

void rodar_testes_mqtt_telemetria(void)
{
    RUN_TEST(test_telemetria_contem_campos);
    RUN_TEST(test_telemetria_nivel_por_estado);
    RUN_TEST(test_telemetria_score_nan_vira_null_e_rotulo_padrao);
    RUN_TEST(test_telemetria_defensivos_e_truncacao);
}
