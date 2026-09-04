/*
 * i2c_config — configuração do barramento I2C (PulsoPNAAT).
 *
 * Isola a criação do barramento master e do dispositivo BNO085 para que
 * `vibration_sensor` (e futuros periféricos I2C) não conheçam pinagem nem
 * endereço — tudo vem do Kconfig (menu "I2C & GPIO (BNO085)").
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicializa o barramento I2C master e adiciona o dispositivo BNO085
 * (pinos, endereço e frequência de CONFIG_APP_BNO085_*).
 *
 * Retorna ESP_OK e preenche os dois handles; falha propaga o erro do driver.
 * Chamar uma única vez, antes de vibration_sensor_start().
 */
esp_err_t i2c_config_init(i2c_master_bus_handle_t *bus_handle_out,
                          i2c_master_dev_handle_t *bno085_dev_handle_out);

#ifdef __cplusplus
}
#endif
