/*
 * wifi_config — conexão Wi-Fi station do nó (PulsoPNAAT).
 *
 * SSID, senha, número de tentativas e modo mínimo de autenticação vêm do
 * Kconfig (menu "WiFi Configuration"). A rede precisa ser 2,4 GHz: o ESP32-S3
 * não opera em 5 GHz. A aplicação só observa estados pelo callback; sem rede
 * o monitoramento local continua (máquina em CONTINGÊNCIA, ver alert_manager).
 */
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi.h"

// Configurações WiFi
#define WIFI_CONFIG_SSID          CONFIG_WIFI_SSID
#define WIFI_CONFIG_PASSWORD      CONFIG_WIFI_PASSWORD
#define WIFI_CONFIG_MAX_RETRY     CONFIG_WIFI_MAX_RETRY
#if defined(CONFIG_WIFI_AUTH_OPEN)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_OPEN
#elif defined(CONFIG_WIFI_AUTH_WEP)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WEP
#elif defined(CONFIG_WIFI_AUTH_WPA_PSK)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WPA_PSK
#elif defined(CONFIG_WIFI_AUTH_WPA2_PSK)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif defined(CONFIG_WIFI_AUTH_WPA_WPA2_PSK)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif defined(CONFIG_WIFI_AUTH_WPA2_ENTERPRISE)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WPA2_ENTERPRISE
#elif defined(CONFIG_WIFI_AUTH_WPA3_PSK)
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_WPA3_PSK
#else
#define WIFI_CONFIG_AUTH_THRESHOLD WIFI_AUTH_OPEN
#endif

// Estados da conexão WiFi
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_FAILED
} wifi_state_t;

// Callback para notificação de estado.
//
// ATENÇÃO DE CONTEXTO: o callback roda na task do loop de eventos ESP
// (system event task), não em task da aplicação. Ele NÃO deve bloquear,
// esperar em fila/mutex com timeout, nem chamar APIs bloqueantes de rede
// (ex.: mqtt_client_start(), esp_wifi_* bloqueantes). Faça apenas log,
// escrita atômica/flag e notificação não-bloqueante (xTaskNotifyGive,
// xQueueSend com timeout 0, alerta_servico_publicar_evento) e deixe o
// trabalho pesado para uma task da aplicação.
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