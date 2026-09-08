idf_component_register(SRCS "baseline.c" "baseline_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES signal_processing nvs_flash)
