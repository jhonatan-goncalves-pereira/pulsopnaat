idf_component_register(SRCS "main.c"
<<<<<<< HEAD
                    INCLUDE_DIRS ".")
=======
                       REQUIRES i2c_config vibration_sensor signal_processing
                                baseline alert_manager driver esp_timer
                                nvs_flash)
>>>>>>> origin/develop
