idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES i2c_config vibration_sensor signal_processing
                                baseline alert_manager wifi_config mqtt_client driver esp_timer
                                nvs_flash storage)