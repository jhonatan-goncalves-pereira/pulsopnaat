/*
 * alerta_servico — face on-device do alert_manager (PulsoPNAAT, ticket 03).
 *
 * O núcleo puro (alert_manager.h) define A LÓGICA; este serviço é o
 * coordenador de hardware: task coordenadora consome a fila de eventos
 * (publicada por botão/serial e pela task de processamento), aplica
 * transitar() e re-renderiza alerta_sinalizar() → LED RGB externo (3 GPIOs)
 * + buzzer a cada tick de 100 ms.
 *
 * - Único ESCRITOR do estado da máquina (task coordenadora); demais tasks
 *   consultam via leitura atômica.
 * - Estado do equipamento publicado pela task de processamento (escrita
 *   atômica de 32 bits), re-renderizado no tick seguinte (≤ 100 ms).
 * - LED: RGB externo de 3 canais discretos (um GPIO por cor, polaridade via
 *   Kconfig) — a Heltec V3 não tem LED RGB endereçável utilizável.
 * - Buzzer ATIVO em GPIO digital (nível via Kconfig).
 *
 * WIFI_CAIR/WIFI_RESTAURADO existem na máquina mas nenhum produtor os emite
 * no ticket 03 (conectividade é o ticket 04) — CONTINGÊNCIA fica wired lá.
 */
#pragma once

#include "esp_err.h"

#include "alert_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cria a fila de eventos e a task coordenadora (core 1, prioridade baixa)
 * e inicializa os canais do LED RGB externo e o buzzer. Estado inicial da
 * máquina: BOOT, LED apagado. Idempotente (2ª chamada retorna ESP_OK sem
 * duplicar). Falha de LED/buzzer não impede o nó: componentes de sinalização
 * caem em "silencioso" (log de erro), o monitoramento segue.
 */
esp_err_t alerta_servico_iniciar(void);

/*
 * Publica um evento para a máquina de estados (transita na task
 * coordenadora). Fila cheia → evento DESCARTADO com log (a máquina tem
 * poucos eventos; perda pontual é corrigida pelo próximo produtor).
 * Seguro antes de iniciar (descarta silenciosamente).
 */
void alerta_servico_publicar_evento(evento_t evento);

/* Publica o estado do equipamento classificado (task de processamento).
 * Escrita atômica; re-render no tick seguinte. */
void alerta_servico_definir_estado_equipamento(
    estado_equipamento_t estado_equipamento);

/* Estado atual da máquina (leitura atômica — thread-safe). */
estado_maquina_t alerta_servico_estado_maquina(void);

/* Último estado do equipamento publicado (leitura atômica). */
estado_equipamento_t alerta_servico_estado_equipamento(void);

#ifdef __cplusplus
}
#endif
