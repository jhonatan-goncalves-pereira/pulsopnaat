/*
 * signal_processing — matemática de sinais por janela, por eixo (PulsoPNAAT).
 *
 * Componente PURAMENTE determinístico: sem dependências de ESP-IDF, sem I/O e
 * sem estado global — os mesmos fontes compilam no alvo e no PC, o que permite
 * testar a matemática do pipeline com entradas sintéticas e saídas esperadas
 * conhecidas (validação on-target via test_app).
 *
 * Fase atual: apenas RMS. As próximas fases (FFT com Hann, kurtosis, THD)
 * estendem metricas_t sem mudar o contrato desta função de análise.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * fs = 500 Hz (taxa nativa do ACCELEROMETER do BNO085, confirmada no log de
 * taxa efetiva) com janela de 1 s → N = 500 amostras por eixo, 3 eixos.
 */
#define JANELA_N_AMOSTRAS 500
#define JANELA_NUM_EIXOS  3

typedef enum {
    EIXO_X = 0,
    EIXO_Y = 1,
    EIXO_Z = 2,
} eixo_t;

/*
 * Janela triaxial de aceleração em m/s², produzida por `vibration_sensor`.
 *
 * `amostras[i][eixo]` — i-ésima amostra da janela, eixo 0=X, 1=Y, 2=Z.
 * `ts_primeira_us` / `ts_ultima_us` — timestamps SH-2 (µs) da primeira e da
 * última amostra; baseiam-se no tempo do sensor (base timestamp + delay por
 * reporte), servindo para medir a taxa efetiva de amostragem.
 * `n_amostras` — amostras efetivamente acumuladas (esperado: JANELA_N_AMOSTRAS;
 * pode ser menor se a robustez futura descartar amostras inválidas).
 */
typedef struct {
    float amostras[JANELA_N_AMOSTRAS][JANELA_NUM_EIXOS];
    uint32_t n_amostras;
    uint64_t ts_primeira_us;
    uint64_t ts_ultima_us;
} janela_t;

/*
 * Métricas calculadas para uma janela, independentemente por eixo.
 * Fase atual: somente RMS, em m/s².
 */
typedef struct {
    float rms[JANELA_NUM_EIXOS];
} metricas_t;

/*
 * Função de análise da janela — PURA e testável no host.
 *
 * Toda a matemática do pipeline (hoje RMS; depois harmônicos via FFT,
 * kurtosis, THD e classificação) deve viver aqui, para que seja verificável
 * isoladamente do hardware. Nas próximas fases a assinatura passa a receber
 * também o baseline (entrada) e a classificação por eixo (saída), sem mudar o
 * contrato de pureza: nenhuma alocação, I/O ou estado global.
 *
 * `janela` NULL → `metricas_out` recebe zeros (defensivo, não é caso normal).
 * `metricas_out` NULL → no-op. `n_amostras` > JANELA_N_AMOSTRAS → limitado.
 */
void analisar_janela(const janela_t *janela, metricas_t *metricas_out);

/*
 * RMS = √(média(x²)) sobre n amostras contíguas, em m/s².
 * Acumulação em double para robustez numérica. NULL/n=0 → 0.0f.
 */
float calcular_rms(const float *amostras, size_t n_amostras);

/*
 * Variante com passo (stride) entre amostras — usada por analisar_janela para
 * calcular o RMS de um eixo dentro do layout intercalado [amostra][eixo]
 * sem copiar dados. NULL/n=0/passo=0 → 0.0f.
 */
float calcular_rms_passo(const float *amostras, size_t n_amostras, size_t passo);

#ifdef __cplusplus
}
#endif
