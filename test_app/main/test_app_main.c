/*
 * Runner dos testes Unity no ALVO (ESP32-S3) — app de teste dedicado.
 * Chama o corpus de cada componente entre UNITY_BEGIN/END; o resumo
 * (N Tests, 0 Failures, OK) sai no console serial.
 *
 *   . $IDF_PATH/export.sh
 *   idf.py -C test_app build flash monitor
 */
#include "unity.h"

#include "signal_processing.h"

extern void rodar_testes_signal_processing(void);
extern void rodar_testes_baseline(void);
extern void rodar_testes_alert_manager(void);
extern void rodar_testes_mqtt_client(void);
extern void rodar_testes_anomaly_detector(void);
extern void rodar_testes_mqtt_telemetria(void);
extern void rodar_testes_regime_classifier(void);

void app_main(void)
{
    /* Tabelas de twiddle do esp-dsp — idempotente; sem isso as métricas
     * espectrais dos testes sairiam zeradas (FFT retorna erro). */
    signal_processing_init();

    UNITY_BEGIN(); /* void em Unity 2.6.0 — só UNITY_END() retorna falhas. */
    rodar_testes_signal_processing();
    rodar_testes_baseline();
    rodar_testes_alert_manager();
    rodar_testes_mqtt_client();
    rodar_testes_anomaly_detector();
    rodar_testes_mqtt_telemetria();
    rodar_testes_regime_classifier();
    UNITY_END();
    /* Resumo já no serial; sistema permanece vivo em idle para leitura
     * tranquila do monitor. */
}
