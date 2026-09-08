/*
 * i2c_config — implementação (ver i2c_config.h).
 */
#include "i2c_config.h"

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "i2c_config";

esp_err_t i2c_config_init(i2c_master_bus_handle_t *bus_handle_out,
                          i2c_master_dev_handle_t *bno085_dev_handle_out)
{
    if (bus_handle_out == NULL || bno085_dev_handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)CONFIG_APP_BNO085_I2C_SDA_GPIO,
        .scl_io_num = (gpio_num_t)CONFIG_APP_BNO085_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, bus_handle_out), TAG,
                        "falha ao criar barramento I2C master");

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_APP_BNO085_I2C_ADDR,
        .scl_speed_hz = CONFIG_APP_BNO085_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(*bus_handle_out, &dev_config,
                                                  bno085_dev_handle_out),
                        TAG, "falha ao adicionar BNO085 ao barramento");

    ESP_LOGI(TAG, "I2C pronta: SDA=%d SCL=%d freq=%d Hz endereço BNO085=0x%02X",
             CONFIG_APP_BNO085_I2C_SDA_GPIO, CONFIG_APP_BNO085_I2C_SCL_GPIO,
             CONFIG_APP_BNO085_I2C_FREQ_HZ, CONFIG_APP_BNO085_I2C_ADDR);
    return ESP_OK;
}
