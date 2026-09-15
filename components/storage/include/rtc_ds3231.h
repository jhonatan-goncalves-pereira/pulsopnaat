/*
 * rtc_ds3231 — driver mínimo do RTC DS3231 (PulsoPNAAT).
 *
 * Registradores BCD 0x00–0x06 (segundos..ano), leitura/escrita direta —
 * sem alarmes, sem SQW, sem sensor de temperatura interno (fora do escopo
 * do datalog). O dispositivo vive no MESMO barramento I2C do BNO085
 * (montado por i2c_config_init em main.c); este driver só adiciona o
 * segundo device ao barramento já criado.
 *
 * Falha de leitura/escrita é sinalizada por ESP_FAIL/ESP_ERR_* — quem chama
 * decide degradar (RF12/RNF09: sem RTC, o log continua com timestamp
 * relativo ao boot, ver storage.c).
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Adiciona o DS3231 ao barramento `bus_handle` já existente (endereço e
 * frequência de CONFIG_PULSOPNAAT_RTC_I2C_ADDR / CONFIG_APP_BNO085_I2C_FREQ_HZ
 * — mesmo bus, mesma velocidade). Não toca no barramento em si.
 */
esp_err_t ds3231_init(i2c_master_bus_handle_t bus_handle,
                      i2c_master_dev_handle_t *dev_handle_out);

/*
 * Lê a hora atual do RTC em `tm_out` (campos tm_year..tm_sec preenchidos;
 * tm_wday/tm_yday/tm_isdst NÃO calculados — não usar sem normalizar se
 * precisar deles). Falha de comunicação I2C ou dado fora de faixa (ex.:
 * bateria descarregada zerando o chip) → ESP_FAIL.
 */
esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev_handle, struct tm *tm_out);

/*
 * Ajusta o RTC a partir de `tm_in` (campos tm_year..tm_sec; os demais são
 * ignorados). Chamado via storage_ajustar_rtc no callback de sync SNTP
 * (main.c) — o DS3231 não se ajusta sozinho.
 */
esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev_handle, const struct tm *tm_in);

#ifdef __cplusplus
}
#endif
