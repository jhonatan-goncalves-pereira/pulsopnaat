/*
 * alerta_servico — implementação (ver header).
 *
 * Task coordenadora: consome a fila de eventos (timeout = tick de LED) e,
 * a cada iteração, re-renderiza a sinalização a partir do estado corrente:
 *   evento → s = transitar(s, evento)   [só esta task escreve s]
 *   tick   → alerta_sinalizar(s, estado_equip) → LED (padrão/blink) + buzzer
 *
 * Padrões de LED (tick de 100 ms) — LED RGB EXTERNO, 3 canais discretos:
 *   NORMAL      verde (G)
 *   ATENCAO     amarelo (R+G) piscando (~1,7 Hz)
 *   CRITICO     vermelho (R) fixo + buzzer
 *   CALIBRANDO  azul (B) piscando rápido (5 Hz) — "não perturbe"
 *   SEM_CONEXAO branco (R+G+B) piscando (1 Hz)
 *   OFF         apagado (BOOT)
 */
#include "alerta_servico.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <stdatomic.h>

static const char *TAG = "alerta";

/* Fila de eventos e task — singleton do serviço (um nó = uma sinalização;
 * o núcleo puro permanece sem estado global). */
#define FILA_EVENTOS_TAM 8
#define TAREFA_CORE 1
#define TAREFA_PRIORIDADE 3
#define TAREFA_STACK 4096
#define TICK_LED_MS 100

static QueueHandle_t s_fila_eventos;
static TaskHandle_t s_tarefa;

/* Estado compartilhado — leitura/escrita atômica (task de processamento
 * publica o estado do equipamento; tasks de comando publicam eventos;
 * somente esta task escreve o estado da máquina). */
static atomic_int s_estado_maquina = ESTADO_MAQ_BOOT;
static atomic_int s_estado_equipamento = ESTADO_EQUIP_VERDE;

/* Hardware de sinalização (canal desativado → cai em silencioso). */
#define CANAL_R 0
#define CANAL_G 1
#define CANAL_B 2
#define NUM_CANAIS_LED 3
static int s_canal_gpio[NUM_CANAIS_LED] = {
    CONFIG_PULSOPNAAT_LED_R_GPIO,
    CONFIG_PULSOPNAAT_LED_G_GPIO,
    CONFIG_PULSOPNAAT_LED_B_GPIO,
};
static bool s_buzzer_ok;
static bool s_buzzer_ligado;

/* ------------------------- Hardware: LED/buzzer ------------------------- */

/* Aplica o nível (respeitando a polaridade do Kconfig) num canal.
 * GPIO negativo = canal não cableado → ignorado. */
static void canal_aplicar(int canal, bool aceso)
{
    const int gpio = s_canal_gpio[canal];
    if (gpio < 0) {
        return;
    }
    const bool nivel_alto = aceso ? (bool)CONFIG_PULSOPNAAT_LED_ATIVO_ALTO
                                  : !CONFIG_PULSOPNAAT_LED_ATIVO_ALTO;
    gpio_set_level((gpio_num_t)gpio, nivel_alto ? 1 : 0);
}

/* Cor = conjunto de canais acesos (mistura física com os resistores série). */
static void led_definir(bool r, bool g, bool b)
{
    canal_aplicar(CANAL_R, r);
    canal_aplicar(CANAL_G, g);
    canal_aplicar(CANAL_B, b);
}

static void buzzer_aplicar(bool ligar)
{
    if (!s_buzzer_ok || ligar == s_buzzer_ligado) {
        return; /* idempotente: sem reescrita no tick */
    }
    const int nivel = ligar ? (CONFIG_PULSOPNAAT_BUZZER_ATIVO_ALTO ? 1 : 0)
                            : (CONFIG_PULSOPNAAT_BUZZER_ATIVO_ALTO ? 0 : 1);
    gpio_set_level(CONFIG_PULSOPNAAT_BUZZER_GPIO, nivel);
    s_buzzer_ligado = ligar;
}

/* Um tick (100 ms) do padrão piscante de cada indicação. */
static void led_renderizar(indicacao_led_t ind, uint32_t tick)
{
    switch (ind) {
    case LED_IND_OFF:
        led_definir(false, false, false);
        break;
    case LED_IND_CALIBRANDO: /* azul, toggle a cada tick → 5 Hz */
        if (tick & 1u) {
            led_definir(false, false, false);
        } else {
            led_definir(false, false, true);
        }
        break;
    case LED_IND_NORMAL: /* verde fixo */
        led_definir(false, true, false);
        break;
    case LED_IND_ATENCAO: /* amarelo (R+G), toggle a cada 3 ticks → ~1,7 Hz */
        if ((tick / 3u) & 1u) {
            led_definir(false, false, false);
        } else {
            led_definir(true, true, false);
        }
        break;
    case LED_IND_CRITICO: /* vermelho fixo: o buzzer carrega a urgência */
        led_definir(true, false, false);
        break;
    case LED_IND_SEM_CONEXAO: /* branco (R+G+B), toggle a cada 5 ticks → 1 Hz */
        if ((tick / 5u) & 1u) {
            led_definir(false, false, false);
        } else {
            led_definir(true, true, true);
        }
        break;
    default:
        led_definir(false, false, false);
        break;
    }
}

/* ----------------------------- Task principal ---------------------------- */

static void tarefa_coordenadora(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    evento_t evento;

    for (;;) {
        /* Entre eventos, o loop serve de tick de renderização (100 ms). */
        if (xQueueReceive(s_fila_eventos, &evento,
                          pdMS_TO_TICKS(TICK_LED_MS)) == pdTRUE) {
            const estado_maquina_t antes =
                (estado_maquina_t)atomic_load(&s_estado_maquina);
            const estado_maquina_t depois = transitar(antes, evento);
            if (depois != antes) {
                atomic_store(&s_estado_maquina, (int)depois);
                ESP_LOGI(TAG, "máquina de estados: %d → %d (evento %d)",
                         (int)antes, (int)depois, (int)evento);
            }
        }

        sinalizacao_t s = {0};
        alerta_sinalizar((estado_maquina_t)atomic_load(&s_estado_maquina),
                         (estado_equipamento_t)atomic_load(&s_estado_equipamento),
                         &s);

        led_renderizar(s.led, tick);
        buzzer_aplicar(s.buzzer);
        tick++;
    }
}

/* --------------------------- API pública -------------------------------- */

void alerta_servico_publicar_evento(evento_t evento)
{
    if (s_fila_eventos == NULL) {
        return; /* serviço ainda não iniciado — descarta */
    }
    if (xQueueSend(s_fila_eventos, &evento, 0) != pdTRUE) {
        ESP_LOGW(TAG, "fila de eventos cheia — evento %d descartado",
                 (int)evento);
    }
}

void alerta_servico_definir_estado_equipamento(
    estado_equipamento_t estado_equipamento)
{
    atomic_store(&s_estado_equipamento, (int)estado_equipamento);
}

estado_maquina_t alerta_servico_estado_maquina(void)
{
    return (estado_maquina_t)atomic_load(&s_estado_maquina);
}

estado_equipamento_t alerta_servico_estado_equipamento(void)
{
    return (estado_equipamento_t)atomic_load(&s_estado_equipamento);
}

esp_err_t alerta_servico_iniciar(void)
{
    if (s_tarefa != NULL) {
        return ESP_OK; /* idempotente */
    }

    /* LED RGB externo: um GPIO de saída por canal (falha aqui só custa a
     * indicação — o canal problemático é desativado com log). */
    for (int canal = 0; canal < NUM_CANAIS_LED; ++canal) {
        const int gpio = s_canal_gpio[canal];
        if (gpio < 0) {
            continue; /* canal deliberadamente não cableado */
        }
        gpio_config_t cfg_canal = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&cfg_canal) == ESP_OK) {
            /* Garante canal apagado já na init, na polaridade correta. */
            canal_aplicar(canal, false);
        } else {
            ESP_LOGE(TAG, "falha ao inicializar canal %d do LED RGB (GPIO %d) "
                          "— canal desativado", canal, gpio);
            s_canal_gpio[canal] = -1;
        }
    }

    /* Buzzer ativo em GPIO — falha só custa o alerta sonoro. */
    gpio_config_t cfg_buzzer = {
        .pin_bit_mask = 1ULL << CONFIG_PULSOPNAAT_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg_buzzer) == ESP_OK) {
        s_buzzer_ok = true;
        gpio_set_level(CONFIG_PULSOPNAAT_BUZZER_GPIO,
                       CONFIG_PULSOPNAAT_BUZZER_ATIVO_ALTO ? 0 : 1);
    } else {
        ESP_LOGE(TAG, "falha ao inicializar buzzer (GPIO %d) — seguindo "
                      "sem alerta sonoro", CONFIG_PULSOPNAAT_BUZZER_GPIO);
    }

    s_fila_eventos = xQueueCreate(FILA_EVENTOS_TAM, sizeof(evento_t));
    if (s_fila_eventos == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(tarefa_coordenadora, "alerta",
                                TAREFA_STACK, NULL, TAREFA_PRIORIDADE,
                                &s_tarefa, TAREFA_CORE) != pdPASS) {
        vQueueDelete(s_fila_eventos);
        s_fila_eventos = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "serviço de alerta ativo: LED RGB GPIO R=%d G=%d B=%d "
                  "(%s), buzzer GPIO %d (%s), máquina em BOOT",
             s_canal_gpio[CANAL_R], s_canal_gpio[CANAL_G],
             s_canal_gpio[CANAL_B],
             CONFIG_PULSOPNAAT_LED_ATIVO_ALTO ? "catodo comum" : "anodo comum",
             CONFIG_PULSOPNAAT_BUZZER_GPIO,
             CONFIG_PULSOPNAAT_BUZZER_ATIVO_ALTO ? "ativo alto" : "ativo baixo");
    return ESP_OK;
}
