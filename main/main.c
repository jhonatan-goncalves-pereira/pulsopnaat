/*
 * Scanner I2C - PulsoPNAAT
 * Uso temporario: valida a ligacao do GY-BNO085 antes de seguir.
 * Alvo: ESP32-S3 (Heltec WiFi LoRa 32 V3), ESP-IDF >= 5.5
 *
 * Esperado no serial: dispositivo em 0x4A (BNO085).
 * Se aparecer 0x4B, o pino ADR esta em VCC - ligue-o no GND ou ajuste o firmware.
 */

#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PINO_SDA        GPIO_NUM_6
#define PINO_SCL        GPIO_NUM_7
#define PINO_RST        GPIO_NUM_4    /* reset do BNO085, ativo baixo */
#define FREQ_I2C_HZ     100000        /* 100 kHz para o scan: mais tolerante a fio comprido */

static const char *TAG = "scan_i2c";

static void liberar_reset_do_sensor(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PINO_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    /* pulso de reset: baixo por 10 ms, depois solta e espera o sensor subir */
    ESP_ERROR_CHECK(gpio_set_level(PINO_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(PINO_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(300));
}

void app_main(void)
{
    ESP_LOGI(TAG, "SDA=GPIO%d  SCL=GPIO%d  RST=GPIO%d", PINO_SDA, PINO_SCL, PINO_RST);

    liberar_reset_do_sensor();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = PINO_SDA,
        .scl_io_num                   = PINO_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao criar o barramento I2C: %s", esp_err_to_name(err));
        return;
    }

    while (1) {
        int achados = 0;

        printf("\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

        for (uint8_t alto = 0; alto < 8; alto++) {
            printf("%02x: ", alto * 16);

            for (uint8_t baixo = 0; baixo < 16; baixo++) {
                uint8_t endereco = alto * 16 + baixo;

                if (endereco < 0x08 || endereco > 0x77) {
                    printf("   ");
                    continue;
                }

                /* probe: envia so o endereco e espera o ACK, sem escrever dado */
                if (i2c_master_probe(bus, endereco, 50) == ESP_OK) {
                    printf("%02x ", endereco);
                    achados++;
                } else {
                    printf("-- ");
                }
            }
            printf("\n");
        }

        if (achados == 0) {
            ESP_LOGW(TAG, "nenhum dispositivo respondeu");
            ESP_LOGW(TAG, "confira: PS0 e PS1 no GND, VCC em 3V3, SDA/SCL nos pinos certos");
        } else {
            ESP_LOGI(TAG, "%d dispositivo(s) encontrado(s)", achados);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}