#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include "esp_wifi.h"

// Configurações WiFi
#define WIFI_CONFIG_SSID          CONFIG_WIFI_SSID
#define WIFI_CONFIG_PASSWORD      CONFIG_WIFI_PASSWORD
#define WIFI_CONFIG_MAX_RETRY     CONFIG_WIFI_MAX_RETRY
#define WIFI_CONFIG_AUTH_THRESHOLD CONFIG_WIFI_AUTH_THRESHOLD

// Estados da conexão WiFi
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_FAILED
} wifi_state_t;

// Callback para notificação de estado
typedef void (*wifi_state_callback_t)(wifi_state_t state);

/**
 * @brief Inicializa o WiFi em modo STA
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t wifi_config_init(void);

/**
 * @brief Inicia conexão WiFi
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t wifi_config_start(void);

/**
 * @brief Para a conexão WiFi
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t wifi_config_stop(void);

/**
 * @brief Verifica se WiFi está conectado
 * @return true se conectado, false caso contrário
 */
bool wifi_config_is_connected(void);

/**
 * @brief Registra callback para mudanças de estado
 * @param callback Função de callback
 */
void wifi_config_register_callback(wifi_state_callback_t callback);

/**
 * @brief Obtém o estado atual do WiFi
 * @return wifi_state_t Estado atual
 */
wifi_state_t wifi_config_get_state(void);

#endif // WIFI_CONFIG_H