/*
 * alert_manager — classificação 3σ/6σ, votação, pior eixo, máquina de
 * estados e sinalização (PulsoPNAAT, ticket 03).
 *
 * NÚCLEO PURO (este header e alert_manager.c): sem ESP-IDF, FreeRTOS, I/O
 * ou estado global — compila no alvo e no PC (SPEC §Testing Decisions,
 * seams 1 e 2). A face on-device vive em alerta_servico.c/h.
 *
 * Três camadas, todas funções puras:
 *
 *   1. Classificação por métrica: valor > média + 3σ → ATENÇÃO;
 *      > média + 6σ → CRÍTICA (estrito — "ultrapassar", RF04; crítica
 *      avaliada primeiro). σ = 0 colapsa os limiares à média (leitura
 *      literal — decisão na SPEC); na prática σ saudável é > 0.
 *
 *   2. Votação por eixo entre as METRICA_NUM métricas (RF04):
 *      VERDE nenhuma em alerta; AMARELO 1–2 em atenção;
 *      VERMELHO 1 crítica OU 3+ em atenção. Estado do equipamento = PIOR
 *      eixo — falha uniaxial não é diluída (ISO 20816-3 por direção).
 *
 *   3. Máquina de estados: BOOT → CALIBRANDO → MONITORANDO ⇄ CONTINGÊNCIA.
 *      MONITORANDO só é alcançado via EVENTO_BASELINE_DISPONIVEL (o nó
 *      nunca classifica sem baseline — RF09/user story 9).
 */
#pragma once

#include <stdbool.h>

#include "baseline.h"
#include "signal_processing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------- 1. Classificação por métrica --------------------- */

typedef enum {
    CLASSIF_NORMAL = 0,
    CLASSIF_ATENCAO,  /* valor > média + 3σ */
    CLASSIF_CRITICA,  /* valor > média + 6σ */
} classificacao_t;

/*
 * Classifica UMA métrica de UM eixo contra o baseline. Estritamente maior
 * ("ultrapassar"). `baseline` NULL, índices fora de faixa ou baseline
 * inválido → CLASSIF_NORMAL (sem baseline válido não há alerta).
 */
classificacao_t alerta_classificar_metrica(const baseline_t *baseline,
                                           metrica_id_t metrica, eixo_t eixo,
                                           float valor);

/* ------------------- 2. Votação por eixo / pior eixo -------------------- */

/* Estado do equipamento (cores da sinalização, RF04/RF06). */
typedef enum {
    ESTADO_EQUIP_VERDE = 0,    /* normal  */
    ESTADO_EQUIP_AMARELO,      /* atenção */
    ESTADO_EQUIP_VERMELHO,     /* crítico */
} estado_equipamento_t;

/*
 * Votação entre as METRICA_NUM métricas DE UM eixo. `metricas`/`baseline`
 * NULL ou baseline inválido → VERDE (defensivo, idem acima).
 */
estado_equipamento_t alerta_classificar_eixo(const baseline_t *baseline,
                                             const metricas_t *metricas,
                                             eixo_t eixo);

/* Pior estado entre os três eixos (estado do equipamento da janela). */
estado_equipamento_t alerta_classificar_janela(const baseline_t *baseline,
                                               const metricas_t *metricas);

/* ------------------------ 3. Máquina de estados ------------------------- */

typedef enum {
    ESTADO_MAQ_BOOT = 0,        /* init de hardware; sem baseline decidido  */
    ESTADO_MAQ_CALIBRANDO,      /* coletando 30 janelas; sem classificação  */
    ESTADO_MAQ_MONITORANDO,     /* classifica, sinaliza (e publica — t.04)  */
    ESTADO_MAQ_CONTINGENCIA,    /* rede indisponível; segue monitorando     */
} estado_maquina_t;

typedef enum {
    EVENTO_INICIAR_CALIBRACAO = 0, /* comando explícito: botão OU serial    */
    EVENTO_BASELINE_DISPONIVEL,    /* 30 janelas completas OU baseline válido
                                    * carregado da NVS no boot              */
    EVENTO_WIFI_CAIR,              /* conectividade perdida (ticket 04)     */
    EVENTO_WIFI_RESTAURADO,        /* reconexão estabelecida (ticket 04)    */
} evento_t;

/*
 * Função de transição PURA (seam 2 da SPEC): sem transição definida →
 * estado inalterado. Ignorados: WIFI_* em BOOT/CALIBRANDO (coleta é local,
 * não interrompida por rede); INICIAR_CALIBRACAO em CALIBRANDO (já em
 * curso) e em CONTINGENCIA (entra em CALIBRANDO só por BOOT/MONITORANDO);
 * BASELINE_DISPONIVEL repetido. Decisões na SPEC §ticket 03.
 */
estado_maquina_t transitar(estado_maquina_t estado, evento_t evento);

/* --------------------------- Sinalização lógica -------------------------- */

/* Indicações do LED (RF06): calibrando/normal/atenção/crítico/sem conexão;
 * BOOT (breve) ainda não indica nada. */
typedef enum {
    LED_IND_OFF = 0,
    LED_IND_CALIBRANDO,
    LED_IND_NORMAL,
    LED_IND_ATENCAO,
    LED_IND_CRITICO,
    LED_IND_SEM_CONEXAO,
} indicacao_led_t;

typedef struct {
    indicacao_led_t led;
    bool buzzer; /* RF06: soa APENAS em estado crítico */
} sinalizacao_t;

/*
 * Mapeia (estado da máquina, estado do equipamento) → sinalização.
 * CALIBRANDO nunca aciona o buzzer (sem classificação durante a coleta);
 * CONTINGENCIA mantém o buzzer SOMENTE se o equipamento segue em vermelho
 * (SPEC §Máquina de estados). `sinal` NULL → no-op; estado inválido → LED
 * apagado.
 */
void alerta_sinalizar(estado_maquina_t estado_maquina,
                      estado_equipamento_t estado_equipamento,
                      sinalizacao_t *sinal);

#ifdef __cplusplus
}
#endif
