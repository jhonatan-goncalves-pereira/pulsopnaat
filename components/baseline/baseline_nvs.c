/* baseline_nvs — persistência NVS (camada fina de I/O; ver header). */
#include "baseline_nvs.h"

#include "nvs.h"

#include <string.h>

/* Namespace e chave fixos do projeto (documentados no header). */
#define NAMESPACE_NVS "pnaat"
#define CHAVE_BASELINE "baseline"

esp_err_t baseline_salvar_nvs(const baseline_t *b)
{
    if (b == NULL || !baseline_valido(b)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t pacote[BASELINE_TAM_PACOTE];
    baseline_empacotar(b, pacote);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE_NVS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(h, CHAVE_BASELINE, pacote, sizeof(pacote));
    if (err == ESP_OK) {
        err = nvs_commit(h); /* sem commit, a gravação pode não sobreviver */
    }
    nvs_close(h);
    return err;
}

esp_err_t baseline_carregar_nvs(baseline_t *b)
{
    if (b == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE_NVS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err; /* ESP_ERR_NVS_NOT_FOUND se nunca calibrado */
    }

    uint8_t pacote[BASELINE_TAM_PACOTE];
    size_t tam = sizeof(pacote);
    err = nvs_get_blob(h, CHAVE_BASELINE, pacote, &tam);
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    if (tam != sizeof(pacote)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!baseline_desempacotar(pacote, b)) {
        /* Registro corrompido ou de outra versão: descartável, nunca
         * interpretado (contrato em baseline.h). */
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}
