idf_component_register(
    SRCS "pnaat_mqtt_client.c" "mqtt_payloads.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt signal_processing alert_manager
)