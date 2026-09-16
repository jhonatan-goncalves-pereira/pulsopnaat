# Aplicativo de teste on-target do PulsoPNAAT (Unity no ESP32-S3).
#
# Compila o corpus de testes de components/*/test/ no Xtensa com o componente
# `unity` do IDF. Uso:
#   . $IDF_PATH/export.sh
#   idf.py -C test_app set-target esp32s3   (uma vez)
#   idf.py -C test_app build flash monitor
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Componentes sob teste — apenas os que têm corpus de teste (cada diretório
# de componente individualmente, para não arrastar dependências de hardware
# como o driver bno085, resolvidas apenas no projeto principal).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/signal_processing"
    "${CMAKE_CURRENT_LIST_DIR}/../components/baseline"
    "${CMAKE_CURRENT_LIST_DIR}/../components/alert_manager"
    "${CMAKE_CURRENT_LIST_DIR}/../components/mqtt_client"
    "${CMAKE_CURRENT_LIST_DIR}/../components/anomaly_detector"
    "${CMAKE_CURRENT_LIST_DIR}/../components/regime_classifier")

project(test_app)
