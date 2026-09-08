idf_component_register(SRCS "alert_manager.c" "alerta_servico.c"
                       INCLUDE_DIRS "include"
                       REQUIRES signal_processing baseline driver)
