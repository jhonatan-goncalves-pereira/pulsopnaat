idf_component_register(
    SRCS "test_app_main.c"
         "../../components/signal_processing/test/test_signal_processing.c"
    PRIV_REQUIRES unity signal_processing
)
