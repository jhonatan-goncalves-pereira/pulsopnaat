idf_component_register(
    SRCS "test_app_main.c"
         "../../components/signal_processing/test/test_signal_processing.c"
         "../../components/baseline/test/test_baseline.c"
         "../../components/alert_manager/test/test_alert_manager.c"
    PRIV_REQUIRES unity signal_processing baseline alert_manager
)
