/*
 * alert_manager — implementação do núcleo puro (ver alert_manager.h).
 *
 * Nada depende de ESP-IDF/FreeRTOS: classificação, votação, pior eixo,
 * transição e sinalização lógica são funções puras sobre signal_processing/
 * baseline — exercitadas on-target via test_app.
 */
#include "alert_manager.h"

#include <stddef.h>

/* Limiares estatísticos (normativo, RF04): atenção = média + 3σ,
 * crítica = média + 6σ. Estrito: "ultrapassar". */
#define SIGMA_ATENCAO 3.0f
#define SIGMA_CRITICO 6.0f

/* --------------------- 1. Classificação por métrica --------------------- */

classificacao_t alerta_classificar_metrica(const baseline_t *baseline,
                                           metrica_id_t metrica, eixo_t eixo,
                                           float valor)
{
    if (baseline == NULL || metrica < 0 || metrica >= METRICA_NUM ||
        eixo < 0 || eixo >= JANELA_NUM_EIXOS || !baseline_valido(baseline)) {
        return CLASSIF_NORMAL;
    }

    const float media = baseline->media[metrica][eixo];
    const float sigma = baseline->desvio_padrao[metrica][eixo];

    /* Crítica primeiro: o valor acima de 6σ também está acima de 3σ, e o
     * estado reportado deve ser o pior (sem isso nenhuma janela chegará a
     * crítica). */
    if (valor > media + SIGMA_CRITICO * sigma) {
        return CLASSIF_CRITICA;
    }
    if (valor > media + SIGMA_ATENCAO * sigma) {
        return CLASSIF_ATENCAO;
    }
    return CLASSIF_NORMAL;
}

/* ------------------- 2. Votação por eixo / pior eixo -------------------- */

/* Pior estado (ordem VERDE < AMARELO < VERMELHO) entre dois candidatos. */
static estado_equipamento_t pior(estado_equipamento_t a, estado_equipamento_t b)
{
    return (a > b) ? a : b;
}

estado_equipamento_t alerta_classificar_eixo(const baseline_t *baseline,
                                             const metricas_t *metricas,
                                             eixo_t eixo)
{
    if (metricas == NULL || eixo < 0 || eixo >= JANELA_NUM_EIXOS) {
        return ESTADO_EQUIP_VERDE;
    }
    if (baseline == NULL || !baseline_valido(baseline)) {
        return ESTADO_EQUIP_VERDE;
    }

    float v[METRICA_NUM][JANELA_NUM_EIXOS];
    metricas_para_vetor(metricas, v);

    int n_atencao = 0;
    int n_critica = 0;
    for (int m = 0; m < METRICA_NUM; ++m) {
        switch (alerta_classificar_metrica(baseline, (metrica_id_t)m, eixo,
                                           v[m][eixo])) {
        case CLASSIF_CRITICA:
            n_critica++;
            break;
        case CLASSIF_ATENCAO:
            n_atencao++;
            break;
        case CLASSIF_NORMAL:
            break;
        }
    }

    /* Votação (RF04): vermelho com 1 crítica OU 3+ em atenção; amarelo com
     * 1–2 em atenção. A votação dilui falsos positivos de uma métrica
     * isolada, mas 3 convergindo já é padrão de falha (não ruído). */
    if (n_critica >= 1 || n_atencao >= 3) {
        return ESTADO_EQUIP_VERMELHO;
    }
    if (n_atencao >= 1) {
        return ESTADO_EQUIP_AMARELO;
    }
    return ESTADO_EQUIP_VERDE;
}

estado_equipamento_t alerta_classificar_janela(const baseline_t *baseline,
                                               const metricas_t *metricas)
{
    estado_equipamento_t estado = ESTADO_EQUIP_VERDE;
    for (int e = 0; e < JANELA_NUM_EIXOS; ++e) {
        estado = pior(estado, alerta_classificar_eixo(baseline, metricas,
                                                      (eixo_t)e));
    }
    return estado;
}

/* ---------------- 2b. Confirmação de estado (persistência) --------------- */

estado_equipamento_t alerta_confirmar_estado(estado_equipamento_t estado_atual,
                                             estado_equipamento_t classificado,
                                             int k_janelas,
                                             confirmacao_estado_t *acc)
{
    if (acc == NULL) {
        return classificado; /* defensivo: sem acumulador, sem confirmação */
    }
    if (k_janelas <= 1) {
        /* Confirmação desativada: classificação vira estado imediatamente
         * (comportamento janela-a-janela, padrão antes da v2.3). */
        acc->candidato = classificado;
        acc->n_consecutivas = 0;
        return classificado;
    }
    if (classificado == estado_atual) {
        /* Janela em linha com o estado vigente: nada pendente. */
        acc->candidato = classificado;
        acc->n_consecutivas = 0;
        return estado_atual;
    }
    if (classificado == acc->candidato) {
        acc->n_consecutivas++;
    } else {
        acc->candidato = classificado;
        acc->n_consecutivas = 1;
    }
    if (acc->n_consecutivas >= k_janelas) {
        /* Confirmado: pendente zerado — as próximas janelas em linha com o
         * novo estado caem na ramificação `classificado == estado_atual`. */
        acc->candidato = classificado;
        acc->n_consecutivas = 0;
        return classificado;
    }
    return estado_atual;
}

/* ------------------------ 3. Máquina de estados ------------------------- */

estado_maquina_t transitar(estado_maquina_t estado, evento_t evento)
{
    switch (estado) {
    case ESTADO_MAQ_BOOT:
        if (evento == EVENTO_INICIAR_CALIBRACAO) {
            return ESTADO_MAQ_CALIBRANDO;
        }
        if (evento == EVENTO_BASELINE_DISPONIVEL) {
            return ESTADO_MAQ_MONITORANDO; /* baseline válido vindo da NVS */
        }
        return ESTADO_MAQ_BOOT; /* WIFI_* não têm efeito antes da rede */

    case ESTADO_MAQ_CALIBRANDO:
        if (evento == EVENTO_BASELINE_DISPONIVEL) {
            return ESTADO_MAQ_MONITORANDO;
        }
        /* Comando repetido e WIFI_* ignorados: a coleta é local e não é
         * interrompida por eventos de rede (decisão em alert_manager.h). */
        return ESTADO_MAQ_CALIBRANDO;

    case ESTADO_MAQ_MONITORANDO:
        if (evento == EVENTO_WIFI_CAIR) {
            return ESTADO_MAQ_CONTINGENCIA;
        }
        if (evento == EVENTO_INICIAR_CALIBRACAO) {
            return ESTADO_MAQ_CALIBRANDO; /* recalibração comandada */
        }
        return ESTADO_MAQ_MONITORANDO;

    case ESTADO_MAQ_CONTINGENCIA:
        if (evento == EVENTO_WIFI_RESTAURADO) {
            return ESTADO_MAQ_MONITORANDO;
        }
        /* WIFI_CAIR repetido e INICIAR_CALIBRACAO ignorados (o diagrama da
         * SPEC entra em CALIBRANDO apenas por BOOT/MONITORANDO). */
        return ESTADO_MAQ_CONTINGENCIA;

    default:
        return ESTADO_MAQ_BOOT;
    }
}

/* --------------------------- Sinalização lógica -------------------------- */

void alerta_sinalizar(estado_maquina_t estado_maquina,
                      estado_equipamento_t estado_equipamento,
                      sinalizacao_t *sinal)
{
    if (sinal == NULL) {
        return;
    }

    sinal->buzzer = false;

    switch (estado_maquina) {
    case ESTADO_MAQ_BOOT:
        sinal->led = LED_IND_OFF; /* init: nada a indicar ainda */
        break;

    case ESTADO_MAQ_CALIBRANDO:
        sinal->led = LED_IND_CALIBRANDO; /* "não perturbe o equipamento" */
        sinal->buzzer = false;           /* sem classificação durante a coleta */
        break;

    case ESTADO_MAQ_MONITORANDO:
        switch (estado_equipamento) {
        case ESTADO_EQUIP_VERDE:
            sinal->led = LED_IND_NORMAL;
            break;
        case ESTADO_EQUIP_AMARELO:
            sinal->led = LED_IND_ATENCAO;
            break;
        case ESTADO_EQUIP_VERMELHO:
            sinal->led = LED_IND_CRITICO;
            sinal->buzzer = true; /* RF06: buzzer APENAS em crítico */
            break;
        default:
            sinal->led = LED_IND_OFF;
            break;
        }
        break;

    case ESTADO_MAQ_CONTINGENCIA:
        sinal->led = LED_IND_SEM_CONEXAO;
        /* Na contingência o monitoramento segue: buzzer permanece ligado
         * apenas se o equipamento segue em vermelho (SPEC). */
        sinal->buzzer = (estado_equipamento == ESTADO_EQUIP_VERMELHO);
        break;

    default:
        sinal->led = LED_IND_OFF;
        break;
    }
}
