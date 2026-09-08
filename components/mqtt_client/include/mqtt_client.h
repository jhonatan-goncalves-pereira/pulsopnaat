#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "esp_mqtt_client.h"
#include <stdbool.h>

// Configurações MQTT
#define MQTT_CLIENT_URI             CONFIG_MQTT_BROKER_URI
#define MQTT_CLIENT_TASK_PRIORITY   CONFIG_MQTT_TASK_PRIORITY
#define MQTT_CLIENT_BUFFER_SIZE     CONFIG_MQTT_BUFFER_SIZE

// Tópicos padrão do projeto Pulsopnaat
#define MQTT_TOPIC_SENSOR_DATA      "pulsopnaat/sensor/data"
#define MQTT_TOPIC_ALERT            "pulsopnaat/alert"
#define MQTT_TOPIC_STATUS           "pulsopnaat/status"
#define MQTT_TOPIC_COMMAND          "pulsopnaat/command"

// Estados do cliente MQTT
typedef enum {
    MQTT_CLIENT_STATE_DISCONNECTED = 0,
    MQTT_CLIENT_STATE_CONNECTING,
    MQTT_CLIENT_STATE_CONNECTED,
    MQTT_CLIENT_STATE_SUBSCRIBED,
    MQTT_CLIENT_STATE_ERROR
} mqtt_client_state_t;

// Callbacks
typedef void (*mqtt_data_callback_t)(const char *topic, const char *data, int data_len);
typedef void (*mqtt_state_callback_t)(mqtt_client_state_t state);

/**
 * @brief Inicializa o cliente MQTT
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Inicia conexão com broker MQTT
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Para o cliente MQTT
 * @return esp_err_t ESP_OK se bem sucedido
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief Publica mensagem em um tópico
 * @param topic Tópico para publicação
 * @param data Dados a serem publicados
 * @param len Tamanho dos dados
 * @param qos QoS (0, 1 ou 2)
 * @return int Message ID ou -1 em caso de erro
 */
int mqtt_client_publish(const char *topic, const char *data, int len, int qos);

/**
 * @brief Inscreve-se em um tópico
 * @param topic Tópico para inscrição
 * @param qos QoS (0, 1 ou 2)
 * @return int Message ID ou -1 em caso de erro
 */
int mqtt_client_subscribe(const char *topic, int qos);

/**
 * @brief Verifica se MQTT está conectado
 * @return true se conectado, false caso contrário
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Registra callback para dados recebidos
 * @param callback Função de callback
 */
void mqtt_client_register_data_callback(mqtt_data_callback_t callback);

/**
 * @brief Registra callback para mudanças de estado
 * @param callback Função de callback
 */
void mqtt_client_register_state_callback(mqtt_state_callback_t callback);

/**
 * @brief Obtém o estado atual do cliente MQTT
 * @return mqtt_client_state_t Estado atual
 */
mqtt_client_state_t mqtt_client_get_state(void);

#endif // MQTT_CLIENT_H