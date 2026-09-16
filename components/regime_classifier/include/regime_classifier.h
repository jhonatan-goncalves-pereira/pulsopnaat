/*
 * regime_classifier — reconhece o regime de operação do equipamento (ventilador da
 * bancada: parado, velocidade 1, 2 ou 3) e avalia a saúde dentro desse regime.
 *
 * PURO: o modelo é treinado offline com o dataset rotulado do próprio equipamento
 * (tools/classificador/treinar_regime.py) e gerado em modelo_regime.h. Por janela:
 * 18 features log do anomaly_detector → padronização → distância de Mahalanobis ao
 * centro de cada regime; vence o regime de menor d² + log|Σ| (gaussiana por classe).
 * A distância ao regime vencedor, comparada aos limiares desse regime, dá a saúde:
 * dentro do esperado (verde), atenção (amarelo) ou crítico (vermelho). Classes marcadas
 * como falha conhecida (bancada: alimentação por filtro de linha com mau contato) são
 * crítico ao serem reconhecidas; a distância segue cobrindo anomalias não vistas no treino.
 *
 * Por que existe: limiares média+kσ calibrados numa única velocidade disparam crítico
 * quando a velocidade muda, mesmo com o equipamento saudável. Conhecendo o regime,
 * a saúde é julgada contra a assinatura daquela velocidade.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alert_manager.h"    /* estado_equipamento_t */
#include "anomaly_detector.h" /* ANOMALIA_N_FEATURES, anomalia_extrair_features */

#ifdef __cplusplus
extern "C" {
#endif

#define REGIME_MAX_CLASSES 5
#define REGIME_DESCONHECIDO (-1)
/* Valor publicado quando a classe reconhecida é a falha conhecida da bancada. */
#define REGIME_FALHA 4
/* Regime exibido com histerese: só troca quando outro regime ocupa REGIME_FILTRO_MINIMO das
 * últimas REGIME_FILTRO_JANELAS janelas. Medido na bancada: com moda simples de 5 janelas a
 * velocidade 3 alternava com a 2 por ~1 min; com 7 de 9 a exibição ficou estável (~4 s de atraso). */
#define REGIME_FILTRO_JANELAS 9
#define REGIME_FILTRO_MINIMO 7

typedef struct {
    int n_classes;
    int valor[REGIME_MAX_CLASSES]; /* publicado: 0 = parado, 1..3 = velocidade, 4 = falha */
    bool falha[REGIME_MAX_CLASSES]; /* classe de falha conhecida: reconhecê-la já é crítico */
    float media_global[ANOMALIA_N_FEATURES];
    float escala[ANOMALIA_N_FEATURES];
    float media[REGIME_MAX_CLASSES][ANOMALIA_N_FEATURES];
    float precisao[REGIME_MAX_CLASSES][ANOMALIA_N_FEATURES][ANOMALIA_N_FEATURES];
    float log_det[REGIME_MAX_CLASSES];
    float limiar_atencao[REGIME_MAX_CLASSES];
    float limiar_critico[REGIME_MAX_CLASSES];
    /* Portão de decisão (§5.4) aprovado no treino: o modelo substitui os limiares no
     * LED, buzzer e alertas. Falso: só informa (modo sombra). */
    bool decide_estado;
} regime_modelo_t;

typedef struct {
    int regime;                  /* valor[] da classe vencedora, ou REGIME_DESCONHECIDO */
    float distancia;             /* Mahalanobis ao regime vencedor; NaN sem modelo */
    estado_equipamento_t estado; /* pela distância e pelos limiares do regime vencedor */
} regime_resultado_t;

/* Classifica uma janela. Modelo ou métricas NULL → REGIME_DESCONHECIDO, NaN, VERDE. */
void regime_classificar(const regime_modelo_t *modelo, const metricas_t *m,
                        regime_resultado_t *out);

typedef struct {
    int historico[REGIME_FILTRO_JANELAS];
    uint32_t n;       /* janelas acumuladas (satura em REGIME_FILTRO_JANELAS) */
    uint32_t proximo; /* posição de escrita circular */
    int atual;        /* regime exibido */
} regime_filtro_t;

void regime_filtro_iniciar(regime_filtro_t *f);

/* Acrescenta o regime da janela e devolve o regime exibido: a primeira janela é adotada na
 * hora; depois, só troca para o regime mais frequente se ele somar REGIME_FILTRO_MINIMO. */
int regime_filtro_atualizar(regime_filtro_t *f, int regime);

/* Modelo gerado em modelo_regime.h, ou NULL enquanto não houver treino. */
const regime_modelo_t *regime_modelo_embarcado(void);

#ifdef __cplusplus
}
#endif
