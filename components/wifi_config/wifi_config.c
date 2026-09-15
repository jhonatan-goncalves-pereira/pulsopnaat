#include "wifi_config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"

static const char *TAG = "wifi_config";
static wifi_state_t s_current_state = WIFI_STATE_DISCONNECTED;
static wifi_state_callback_t s_state_callback = NULL;
static int s_retry_count = 0;

/* RNF05: depois de esgotar as tentativas imediatas, nova rodada a cada 30 s até a rede voltar. */
#define WIFI_RECONEXAO_PERIODO_US (30LL * 1000000LL)
static esp_timer_handle_t s_reconexao_timer;

static void reconexao_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "RNF05: nova rodada de reconexão ao Wi-Fi");
    s_retry_count = 0;
    s_current_state = WIFI_STATE_CONNECTING;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect falhou: %s — reagendando", esp_err_to_name(err));
        esp_timer_start_once(s_reconexao_timer, WIFI_RECONEXAO_PERIODO_US);
    }
}

// Event handlers
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

esp_err_t wifi_config_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta falhou");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = reconexao_timer_cb,
        .name = "wifi_reconexao",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconexao_timer));

    ESP_LOGI(TAG, "WiFi config initialized");
    return ESP_OK;
}

esp_err_t wifi_config_start(void)
{
    const char *ssid = WIFI_CONFIG_SSID;
    const char *pass = WIFI_CONFIG_PASSWORD;
    const size_t len_ssid = strlen(ssid);
    const size_t len_pass = strlen(pass);

    if (len_ssid == 0) {
        ESP_LOGE(TAG, "SSID vazio (CONFIG_WIFI_SSID) — configure via menuconfig");
        return ESP_ERR_INVALID_ARG;
    }
    if (len_ssid > sizeof(((wifi_config_t *)0)->sta.ssid)) {
        ESP_LOGE(TAG, "SSID com %u chars excede %u",
                 (unsigned)len_ssid,
                 (unsigned)sizeof(((wifi_config_t *)0)->sta.ssid));
        return ESP_ERR_INVALID_ARG;
    }
    if (len_pass > sizeof(((wifi_config_t *)0)->sta.password) - 1) {
        ESP_LOGE(TAG, "Senha com %u chars excede %u",
                 (unsigned)len_pass,
                 (unsigned)(sizeof(((wifi_config_t *)0)->sta.password) - 1));
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, ssid, len_ssid);
    memcpy(wifi_config.sta.password, pass, len_pass);
    wifi_config.sta.threshold.authmode = (wifi_auth_mode_t)WIFI_CONFIG_AUTH_THRESHOLD;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    s_retry_count = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started. Connecting to SSID: %s", ssid);
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
            case WIFI_EVENT_STA_START: {
                s_retry_count = 0;
                s_current_state = WIFI_STATE_CONNECTING;
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "esp_wifi_connect falhou: %s", esp_err_to_name(err));
                }
                if (s_state_callback) {
                    s_state_callback(s_current_state);
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                if (s_retry_count < WIFI_CONFIG_MAX_RETRY) {
                    s_retry_count++;
                    s_current_state = WIFI_STATE_DISCONNECTED;
                    ESP_LOGI(TAG, "Desconectado (%d/%d), tentando reconectar",
                             s_retry_count, WIFI_CONFIG_MAX_RETRY);
                    if (s_state_callback) {
                        s_state_callback(s_current_state);
                    }
                    esp_err_t err = esp_wifi_connect();
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "esp_wifi_connect falhou: %s", esp_err_to_name(err));
                    }
                } else {
                    s_current_state = WIFI_STATE_FAILED;
                    ESP_LOGE(TAG, "WiFi falhou após %d tentativas — nova rodada em %lld s (RNF05)",
                             s_retry_count, (long long)(WIFI_RECONEXAO_PERIODO_US / 1000000LL));
                    esp_timer_start_once(s_reconexao_timer, WIFI_RECONEXAO_PERIODO_US);
                    if (s_state_callback) {
                        s_state_callback(s_current_state);
                    }
                }
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        s_retry_count = 0;
        s_current_state = WIFI_STATE_GOT_IP;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_state_callback) {
            s_state_callback(s_current_state);
        }
    }
}
