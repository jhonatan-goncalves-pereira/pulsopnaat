idf_component_register(
    SRCS "test_app_main.c"
         "../../components/signal_processing/test/test_signal_processing.c"
         "../../components/baseline/test/test_baseline.c"
         "../../components/alert_manager/test/test_alert_manager.c"
         "../../components/mqtt_client/test/test_mqtt_client.c"
    PRIV_REQUIRES unity signal_processing baseline alert_manager mqtt_client
)
