#include "wifi_config.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_config";
static wifi_state_t s_current_state = WIFI_STATE_DISCONNECTED;
static wifi_state_callback_t s_state_callback = NULL;

// Event handlers
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

esp_err_t wifi_config_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    ESP_LOGI(TAG, "WiFi config initialized");
    return ESP_OK;
}

esp_err_t wifi_config_start(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_CONFIG_SSID,
            .password = WIFI_CONFIG_PASSWORD,
            .threshold.authmode = WIFI_CONFIG_AUTH_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi started. Connecting to SSID: %s", WIFI_CONFIG_SSID);
    return ESP_OK;
}

esp_err_t wifi_config_stop(void)
{
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

bool wifi_config_is_connected(void)
{
    return (s_current_state == WIFI_STATE_GOT_IP);
}

void wifi_config_register_callback(wifi_state_callback_t callback)
{
    s_state_callback = callback;
}

wifi_state_t wifi_config_get_state(void)
{
    return s_current_state;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                s_current_state = WIFI_STATE_CONNECTING;
                esp_wifi_connect();
                if (s_state_callback) {
                    s_state_callback(s_current_state);
                }
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                s_current_state = WIFI_STATE_DISCONNECTED;
                ESP_LOGI(TAG, "Disconnected from WiFi");
                if (s_state_callback) {
                    s_state_callback(s_current_state);
                }
                // Tentar reconectar
                esp_wifi_connect();
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        s_current_state = WIFI_STATE_GOT_IP;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_state_callback) {
            s_state_callback(s_current_state);
        }
    }
}