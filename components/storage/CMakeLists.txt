idf_component_register(SRCS "storage.c" "rtc_ds3231.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_timer fatfs sdmmc alert_manager signal_processing)
