/*
 * baseline_nvs — persistência do baseline em NVS (PulsoPNAAT, ticket 03).
 *
 * Camada FINA de I/O sobre o registro empacotado puro (baseline.h): toda a
 * lógica (mágica, versão, CRC, validação) vive em baseline.c e é testável;
 * aqui só há nvs_* — blob único de BASELINE_TAM_PACOTE bytes.
 *
 * Namespace "pnaat", chave "baseline". Sobrevive a reboot: carregado no
 * boot (app_main) quando presente e válido, evitando recalcular a
 * calibração a cada reinício (RF08).
 */
#pragma once

#include "esp_err.h"

#include "baseline.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Grava `b` empacotado (mágica+versão+CRC) na NVS. Requer nvs_flash_init()
 * prévio. `b` inválido (baseline_valido == false) → ESP_ERR_INVALID_ARG
 * (nunca persistir um baseline inutilizável).
 */
esp_err_t baseline_salvar_nvs(const baseline_t *b);

/*
 * Lê e valida o baseline da NVS. ESP_ERR_NVS_NOT_FOUND se nunca calibrado;
 * ESP_ERR_INVALID_CRC/ESP_ERR_INVALID_SIZE se o registro existir mas
 * estiver corrompido/de outra versão (app_main trata como "sem baseline"
 * e loga). `b` NULL → ESP_ERR_INVALID_ARG.
 */
esp_err_t baseline_carregar_nvs(baseline_t *b);

#ifdef __cplusplus
}
#endif
