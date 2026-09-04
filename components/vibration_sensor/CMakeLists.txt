idf_component_register(SRCS "vibration_sensor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver bno085 signal_processing)
