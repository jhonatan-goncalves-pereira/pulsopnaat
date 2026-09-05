/*
 * baseline — modo de calibração e estatísticas do regime saudável
 * (PulsoPNAAT, ticket 03).
 *
 * Perfil de vibração EM OPERAÇÃO SAUDÁVEL: média e σ por métrica (RMS, 1x,
 * 2x, banda 3x–5x, kurtosis, THD) e por eixo (X, Y, Z) sobre 30 janelas de
 * 1 s. Limiares de classificação são ESTATÍSTICOS sobre este perfil —
 * média + 3σ (atenção) e média + 6σ (crítico), não multiplicativos —
 * robustos quando a referência saudável é próxima de zero (RF08).
 *
 * Disparo: SEMPRE comandado (botão/serial) com o motor em regime saudável —
 * nunca automático no boot.
 *
 * Núcleo puro (acumulador, extração, validação e serialização): sem
 * ESP-IDF, I/O ou alocação. Persistência NVS isolada em baseline_nvs.c/h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "signal_processing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nº de janelas saudáveis coletadas na calibração (normativo, RF08). */
#define BASELINE_N_JANELAS 30

/*
 * Baseline calibrado: média e σ POPULACIONAL (÷ n — coerente com a kurtosis
 * do signal_processing) por métrica e eixo, índices na ordem de
 * `metrica_id_t` × `eixo_t`.
 */
typedef struct {
    float media[METRICA_NUM][JANELA_NUM_EIXOS];
    float desvio_padrao[METRICA_NUM][JANELA_NUM_EIXOS];
} baseline_t;

/*
 * Acumulador da calibração — média/variância incrementais (Welford:
 * estável, uma passada, sem armazenar as 30 janelas). Usado pela task de
 * processamento enquanto a máquina está em CALIBRANDO.
 */
typedef struct {
    uint32_t n; /* janelas já acumuladas (≤ BASELINE_N_JANELAS) */
    double media[METRICA_NUM][JANELA_NUM_EIXOS];
    double m2[METRICA_NUM][JANELA_NUM_EIXOS]; /* soma dos quadrados dos desvios */
} baseline_calibracao_t;

/* Reinicia o acumulador (n=0, médias/M2 zeradas). NULL → no-op. */
void baseline_calibracao_iniciar(baseline_calibracao_t *cal);

/*
 * Acrescenta as métricas de UMA janela ao acumulador. Retorna true quando a
 * coleta atinge BASELINE_N_JANELAS (e em toda chamada subsequente); janelas
 * excedentes são ignoradas. `cal`/`metricas` NULL → false.
 */
bool baseline_calibracao_adicionar(baseline_calibracao_t *cal,
                                   const metricas_t *metricas);

/* true quando o acumulador já tem BASELINE_N_JANELAS janelas. NULL → false. */
bool baseline_calibracao_completa(const baseline_calibracao_t *cal);

/*
 * Extrai o baseline do acumulador (média e σ = √(M2/n) por métrica e eixo).
 * Retorna false (e não toca `out`) se a coleta não estiver completa ou
 * argumentos forem NULL.
 */
bool baseline_calibracao_extrair(const baseline_calibracao_t *cal,
                                 baseline_t *out);

/*
 * Baseline utilizável? Valores finitos e σ ≥ 0. Usado antes de entrar em
 * MONITORANDO. NULL → false.
 */
bool baseline_valido(const baseline_t *b);

/* ---------------------------------------------------------------------- */
/* Registro serializado (persistência) — puro e testável.                 */
/* ---------------------------------------------------------------------- */

/*
 * Layout do registro empacotado (156 bytes, little-endian):
 *   [0..3]   mágica "PNB1"
 *   [4..7]   versão do layout (uint32, = 1)
 *   [8..79]  media[METRICA_NUM][JANELA_NUM_EIXOS]  (float32, ordem de linha)
 *   [80..151] desvio_padrao[ídem]
 *   [152..155] CRC32 (IEEE refletido, poly 0xEDB88320) sobre [0..151]
 */
#define BASELINE_TAM_PACOTE 156

/* Serializa `b` no registro com mágica/versão/CRC. `pacote` NULL → no-op;
 * `b` NULL → no-op. */
void baseline_empacotar(const baseline_t *b, uint8_t *pacote);

/*
 * Valida e desserializa o registro (mágica, versão, CRC e `baseline_valido`)
 * em `out`. Retorna false (e não toca `out`) em qualquer inconsistência —
 * registro corrompido ou de outra versão é descartado, nunca interpretado.
 */
bool baseline_desempacotar(const uint8_t *pacote, baseline_t *out);

#ifdef __cplusplus
}
#endif
