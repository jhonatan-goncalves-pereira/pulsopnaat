#include "pnaat_mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"

static const char *TAG = "mqtt_client";
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_client_state_t s_current_state = MQTT_CLIENT_STATE_DISCONNECTED;
static mqtt_data_callback_t s_data_callback = NULL;
static mqtt_state_callback_t s_state_callback = NULL;

// Event handlers
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data);

esp_err_t mqtt_client_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_CLIENT_URI,
        .task.priority = MQTT_CLIENT_TASK_PRIORITY,
        .buffer.size = MQTT_CLIENT_BUFFER_SIZE,
    };
    
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, 
                                   mqtt_event_handler, NULL);
    
    ESP_LOGI(TAG, "MQTT client initialized");
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "MQTT client started");
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_OK;
    }
    
    esp_err_t err = esp_mqtt_client_stop(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

int mqtt_client_publish(const char *topic, const char *data, int len, int qos)
{
    if (s_mqtt_client == NULL || !mqtt_client_is_connected()) {
        ESP_LOGE(TAG, "Cannot publish: client not connected");
        return -1;
    }
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, len, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return -1;
    }
    
    ESP_LOGD(TAG, "Published to %s (msg_id: %d)", topic, msg_id);
    return msg_id;
}

int mqtt_client_subscribe(const char *topic, int qos)
{
    if (s_mqtt_client == NULL || !mqtt_client_is_connected()) {
        ESP_LOGE(TAG, "Cannot subscribe: client not connected");
        return -1;
    }
    
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
        return -1;
    }
    
    ESP_LOGD(TAG, "Subscribed to %s (msg_id: %d)", topic, msg_id);
    return msg_id;
}

bool mqtt_client_is_connected(void)
{
    return (s_current_state == MQTT_CLIENT_STATE_CONNECTED || 
            s_current_state == MQTT_CLIENT_STATE_SUBSCRIBED);
}

void mqtt_client_register_data_callback(mqtt_data_callback_t callback)
{
    s_data_callback = callback;
}

void mqtt_client_register_state_callback(mqtt_state_callback_t callback)
{
    s_state_callback = callback;
}

mqtt_client_state_t mqtt_client_get_state(void)
{
    return s_current_state;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_current_state = MQTT_CLIENT_STATE_CONNECTED;
            ESP_LOGI(TAG, "MQTT connected");
            if (s_state_callback) {
                s_state_callback(s_current_state);
            }
            // Inscrever-se nos tópicos padrão
            mqtt_client_subscribe(MQTT_TOPIC_COMMAND, 1);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            s_current_state = MQTT_CLIENT_STATE_DISCONNECTED;
            ESP_LOGI(TAG, "MQTT disconnected");
            if (s_state_callback) {
                s_state_callback(s_current_state);
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            s_current_state = MQTT_CLIENT_STATE_SUBSCRIBED;
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            if (s_state_callback) {
                s_state_callback(s_current_state);
            }
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT data received, topic=%.*s", event->topic_len, event->topic);
            if (s_data_callback) {
                char topic[event->topic_len + 1];
                memcpy(topic, event->topic, event->topic_len);
                topic[event->topic_len] = '\0';
                
                char data[event->data_len + 1];
                memcpy(data, event->data, event->data_len);
                data[event->data_len] = '\0';
                
                s_data_callback(topic, data, event->data_len);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            s_current_state = MQTT_CLIENT_STATE_ERROR;
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", 
                         event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", 
                         event->error_handle->esp_tls_stack_err);
            }
            if (s_state_callback) {
                s_state_callback(s_current_state);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
}