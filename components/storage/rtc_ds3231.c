/*
 * rtc_ds3231 — implementação (ver rtc_ds3231.h).
 */
#include "rtc_ds3231.h"

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "rtc_ds3231";

#define DS3231_TIMEOUT_MS 250
#define DS3231_REG_SEGUNDOS 0x00

static inline uint8_t bcd_para_dec(uint8_t bcd) { return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F)); }
static inline uint8_t dec_para_bcd(uint8_t dec) { return (uint8_t)(((dec / 10) << 4) | (dec % 10)); }

esp_err_t ds3231_init(i2c_master_bus_handle_t bus_handle,
                      i2c_master_dev_handle_t *dev_handle_out)
{
    if (bus_handle == NULL || dev_handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_PULSOPNAAT_RTC_I2C_ADDR,
        .scl_speed_hz = CONFIG_APP_BNO085_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_config, dev_handle_out),
                        TAG, "falha ao adicionar DS3231 ao barramento (endereço 0x%02X)",
                        CONFIG_PULSOPNAAT_RTC_I2C_ADDR);

    /* Sonda: lê o registrador de segundos. Se não responder (RTC ausente ou
     * endereçamento errado), reporta erro e deixa quem chamou decidir —
     * neste projeto, storage.c degrada para timestamp relativo ao boot. */
    uint8_t reg = DS3231_REG_SEGUNDOS;
    uint8_t dummy;
    const esp_err_t err = i2c_master_transmit_receive(*dev_handle_out, &reg, 1, &dummy, 1,
                                                       DS3231_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 não respondeu em 0x%02X (%s) — RTC indisponível",
                 CONFIG_PULSOPNAAT_RTC_I2C_ADDR, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DS3231 pronto em 0x%02X (mesmo barramento do BNO085)",
             CONFIG_PULSOPNAAT_RTC_I2C_ADDR);
    return ESP_OK;
}

esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev_handle, struct tm *tm_out)
{
    if (dev_handle == NULL || tm_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = DS3231_REG_SEGUNDOS;
    uint8_t buf[7]; /* seg, min, hora, dia-semana, data, mês(+século), ano */
    esp_err_t err = i2c_master_transmit_receive(dev_handle, &reg, 1, buf, sizeof(buf),
                                                DS3231_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t segundos = bcd_para_dec(buf[0] & 0x7F);
    const uint8_t minutos = bcd_para_dec(buf[1] & 0x7F);
    /* Hora: força modo 24h (bit 6 do registrador) — ignora bit 6 na leitura,
     * assume-se que ds3231_set_time sempre grava em 24h. */
    const uint8_t horas = bcd_para_dec(buf[2] & 0x3F);
    const uint8_t dia_mes = bcd_para_dec(buf[4] & 0x3F);
    const uint8_t mes = bcd_para_dec(buf[5] & 0x1F);
    const uint8_t ano_2d = bcd_para_dec(buf[6]);

    if (segundos > 59 || minutos > 59 || horas > 23 || dia_mes < 1 || dia_mes > 31 ||
        mes < 1 || mes > 12) {
        /* Registrador ilegível/zerado (ex.: bateria de backup descarregada
         * após corte de energia) — dado não confiável. */
        ESP_LOGW(TAG, "leitura do DS3231 fora de faixa — bateria de backup?");
        return ESP_FAIL;
    }

    tm_out->tm_sec = segundos;
    tm_out->tm_min = minutos;
    tm_out->tm_hour = horas;
    tm_out->tm_mday = dia_mes;
    tm_out->tm_mon = mes - 1;       /* struct tm: mês 0–11 */
    tm_out->tm_year = ano_2d + 100; /* struct tm: anos desde 1900; assume 20xx */
    tm_out->tm_wday = 0;
    tm_out->tm_yday = 0;
    tm_out->tm_isdst = 0;
    return ESP_OK;
}

esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev_handle, const struct tm *tm_in)
{
    if (dev_handle == NULL || tm_in == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tm_in->tm_year < 100 || tm_in->tm_mon < 0 || tm_in->tm_mon > 11) {
        return ESP_ERR_INVALID_ARG; /* só aceita anos 2000–2099 */
    }

    const uint8_t payload[8] = {
        DS3231_REG_SEGUNDOS,
        dec_para_bcd((uint8_t)tm_in->tm_sec),
        dec_para_bcd((uint8_t)tm_in->tm_min),
        dec_para_bcd((uint8_t)tm_in->tm_hour), /* bit 6 = 0 → modo 24h */
        dec_para_bcd(1),                       /* dia da semana: não usado, grava 1 */
        dec_para_bcd((uint8_t)tm_in->tm_mday),
        dec_para_bcd((uint8_t)(tm_in->tm_mon + 1)),
        dec_para_bcd((uint8_t)(tm_in->tm_year - 100)),
    };
    return i2c_master_transmit(dev_handle, payload, sizeof(payload), DS3231_TIMEOUT_MS);
}
