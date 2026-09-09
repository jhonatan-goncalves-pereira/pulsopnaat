/*
 * Teste da camada de sinalizacao - PulsoPNAAT (Etapa 3)
 * Valida LED RGB de catodo comum e buzzer ativo antes da integracao.
 * Alvo: ESP32-S3 (Heltec WiFi LoRa 32 V3), ESP-IDF >= 5.5
 *
 * Sequencia: cada canal isolado, depois as cores compostas que o
 * alert_manager usa de verdade, depois o buzzer.
 */

#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PINO_LED_R      GPIO_NUM_39
#define PINO_LED_G      GPIO_NUM_40
#define PINO_LED_B      GPIO_NUM_41
#define PINO_BUZZER     GPIO_NUM_42

/* catodo comum: nivel alto acende. Anodo comum: inverta para 0. */
#define NIVEL_ACENDE    1

static const char *TAG = "sinalizacao";

static void configurar_saidas(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PINO_LED_R) | (1ULL << PINO_LED_G) |
                        (1ULL << PINO_LED_B) | (1ULL << PINO_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static void pintar(int r, int g, int b)
{
    gpio_set_level(PINO_LED_R, r ? NIVEL_ACENDE : !NIVEL_ACENDE);
    gpio_set_level(PINO_LED_G, g ? NIVEL_ACENDE : !NIVEL_ACENDE);
    gpio_set_level(PINO_LED_B, b ? NIVEL_ACENDE : !NIVEL_ACENDE);
}

static void apagar(void)
{
    pintar(0, 0, 0);
    gpio_set_level(PINO_BUZZER, 0);
}

static void mostrar(const char *nome, int r, int g, int b, int ms)
{
    ESP_LOGI(TAG, "%s", nome);
    pintar(r, g, b);
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void bipar(int vezes, int ms_ligado, int ms_pausa)
{
    for (int i = 0; i < vezes; i++) {
        gpio_set_level(PINO_BUZZER, 1);
        vTaskDelay(pdMS_TO_TICKS(ms_ligado));
        gpio_set_level(PINO_BUZZER, 0);
        vTaskDelay(pdMS_TO_TICKS(ms_pausa));
    }
}

void app_main(void)
{
    configurar_saidas();
    apagar();

    ESP_LOGI(TAG, "LED R=GPIO%d G=GPIO%d B=GPIO%d, buzzer=GPIO%d",
             PINO_LED_R, PINO_LED_G, PINO_LED_B, PINO_BUZZER);

    while (1) {
        ESP_LOGI(TAG, "--- canais isolados ---");
        mostrar("vermelho puro (so o canal R deve acender)", 1, 0, 0, 1200);
        apagar();
        vTaskDelay(pdMS_TO_TICKS(300));

        mostrar("verde puro (so o canal G)", 0, 1, 0, 1200);
        apagar();
        vTaskDelay(pdMS_TO_TICKS(300));

        mostrar("azul puro (so o canal B)", 0, 0, 1, 1200);
        apagar();
        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_LOGI(TAG, "--- cores dos estados do projeto ---");
        mostrar("normal: verde fixo", 0, 1, 0, 1500);
        mostrar("atencao: amarelo (R+G)", 1, 1, 0, 1500);
        mostrar("critico: vermelho fixo", 1, 0, 0, 1500);
        mostrar("calibrando: azul", 0, 0, 1, 1500);
        mostrar("sem rede: branco (R+G+B)", 1, 1, 1, 1500);
        apagar();
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGI(TAG, "--- buzzer: 3 bipes curtos ---");
        bipar(3, 150, 250);

        ESP_LOGI(TAG, "--- ciclo completo, repetindo em 3 s ---\n");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}