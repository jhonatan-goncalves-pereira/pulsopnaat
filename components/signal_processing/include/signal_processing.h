/*
 * signal_processing — matemática de sinais por janela, por eixo (PulsoPNAAT).
 *
 * Componente PURO: sem FreeRTOS, I/O, alocação ou estado mutável — os mesmos
 * fontes compilam no alvo e no PC (validação on-target via test_app). Única
 * dependência é o esp-dsp (cálculo puro; tabelas criadas por
 * `signal_processing_init()` no boot).
 *
 * Cadeia de métricas por eixo, a cada janela de 1 s:
 *   RMS                        → nível geral de vibração (m/s²)
 *   Harmônicos 1x e 2x (f0, 2f0) → desbalanceamento, desalinhamento
 *   Banda 3x–5x (máx em [3f0, 5f0]) → folga mecânica
 *   Kurtosis (excesso de Fisher) → impulsividade no domínio do tempo
 *   THD (harmônicos 1x–5x)     → degradação espectral geral
 *
 * f0 = RPM_nominal/60 vem da config de boot e é passado como parâmetro —
 * a função permanece pura, sem ler configuração nem relógio.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fs = 500 Hz (taxa nativa do ACCELEROMETER do BNO085) × janela de 1 s. */
#define JANELA_N_AMOSTRAS 500
#define JANELA_NUM_EIXOS  3
#define JANELA_FS_NOMINAL_HZ 500.0f

/* Amostras da janela (Hann + zero-padding para 512): resolução ≈ 0,98 Hz,
 * Nyquist = 250 Hz (RF03). */
#define ANALISE_FFT_N 512

/* Harmônicos 1x–5x usados no THD. */
#define ANALISE_N_HARMONICOS 5

typedef enum {
    EIXO_X = 0,
    EIXO_Y = 1,
    EIXO_Z = 2,
} eixo_t;

/*
 * Janela triaxial de aceleração em m/s², produzida por `vibration_sensor`.
 * `ts_primeira_us`/`ts_ultima_us`: timestamps SH-2 (µs) usados para medir a
 * taxa efetiva de amostragem. `n_amostras`: acumuladas de fato (esperado:
 * JANELA_N_AMOSTRAS).
 */
typedef struct {
    float amostras[JANELA_N_AMOSTRAS][JANELA_NUM_EIXOS];
    uint32_t n_amostras;
    uint64_t ts_primeira_us;
    uint64_t ts_ultima_us;
} janela_t;

/* Métricas de uma janela, por eixo. rms/harmonicas/banda em m/s² (normalizadas
 * pelo ganho coerente da Hann); kurtosis/thd adimensionais. */
typedef struct {
    /* RMS = √(média(x²)) — inclui DC (gravidade ≈ 9,81 m/s² em repouso). */
    float rms[JANELA_NUM_EIXOS];
    /* Amplitude espectral no bin mais próximo de f0 (1x) e de 2·f0 (2x). */
    float harmonica_1x[JANELA_NUM_EIXOS];
    float harmonica_2x[JANELA_NUM_EIXOS];
    /* Amplitude máxima na banda [3·f0, 5·f0]. */
    float banda_3x_5x[JANELA_NUM_EIXOS];
    /*
     * Excesso de Fisher: m4/m2² − 3 (momentos viesados, ÷ n).
     * Gaussiana → 0; senoide pura → −1,5; impulsiva → alto; sinal
     * constante → 0.
     */
    float kurtosis[JANELA_NUM_EIXOS];
    /*
     * THD = √(V₂²+…+V₅²)/V₁. Definido como 0 quando a fundamental não se
     * destaca do ruído (V₁ ≤ 10⁻³·RMS) — sem a guarda, janelas sem 1x
     * produzem razão entre vazamentos de FFT, inútil para a classificação.
     */
    float thd[JANELA_NUM_EIXOS];
} metricas_t;

/* Identificador tabular [métrica][eixo] usado por baseline e alert_manager.
 * A ordem DEVE espelhar os campos da struct (garantido por _Static_assert). */
typedef enum {
    METRICA_RMS = 0,
    METRICA_HARMONICA_1X,
    METRICA_HARMONICA_2X,
    METRICA_BANDA_3X_5X,
    METRICA_KURTOSIS,
    METRICA_THD,
    METRICA_NUM
} metrica_id_t;

/* Chamar UMA VEZ no boot antes de qualquer `analisar_janela` (idempotente).
 * Sem init, a FFT retorna erro e as métricas espectrais saem zeradas. */
void signal_processing_init(void);

/*
 * Análise completa da janela — PURA. Não reentrante: usa buffers de rascunho
 * estáticos (consumo por uma única task de processamento, SPEC).
 *
 * `f0_hz ≤ 0` → métricas espectrais zeradas (RMS/kurtosis ainda calculadas).
 * O mapeamento de bins usa a fs EFETIVA da janela (timestamps SH-2), com
 * fallback para a nominal. `janela` NULL → métricas zeradas;
 * `metricas_out` NULL → no-op; n > JANELA_N_AMOSTRAS → limitado.
 */
void analisar_janela(const janela_t *janela, float f0_hz, metricas_t *metricas_out);

/* Copia as métricas para a visão tabular `saida[métrica][eixo]` (consumo de
 * baseline/alert_manager). `metricas_out` NULL → saída zerada; `saida` NULL →
 * no-op. */
void metricas_para_vetor(const metricas_t *metricas_out,
                         float saida[METRICA_NUM][JANELA_NUM_EIXOS]);

/* Backstop RNF07: leituras > 20 g (ou não-finitas) são fisicamente
 * impossíveis no BNO085 e indicam corrupção de transporte — rejeitar já na
 * aquisição. */
#define AMOSTRA_LIMITE_FISICO_MPS2 196.133f  /* 20 g */

/* Predicado de validade física (RNF07, backstop). Não detecta glitches
 * plausíveis (0,2–3 g) — esses cabem ao Hampel (`janela_hampel`). */
bool amostra_valida(float a_mps2);

/* Janela Hampel de 2k+1 = 11 amostras (~22 ms a 500 Hz): suprime impulsos
 * isolados sem atenuar as bandas de 1x–5x (≥ 25 Hz p/ RPM 1500). */
#define HAMPEL_K_VIZINHOS 5
/* Limiar em MADs. 5·MAD ≈ 5σ gaussiano: senoide + harmônicos ficam em ~1·MAD
 * (não comem vibração real) e o glitch isolado do I2C destoa >10·MAD. Valor
 * anterior 3.0 substituía ~21% de vibração legítima (medido on-target). */
#define HAMPEL_LIMIAR_MAD 5.0f

/*
 * Filtro de Hampel (despike) por eixo — PURA. Para cada amostra: se
 * |x[i]−mediana| > t·MAD da vizinhança de 2k+1 (bordas encolhem), substitui
 * pela mediana (mantém n_amostras e o ritmo). Racional: glitches do I2C/SHTP
 * são plausíveis (0,2–3 g) mas destoam dos vizinhos e inflacionam
 * RMS/kurtosis; substitui — não descarta — para não viciar a taxa efetiva.
 * In-place. `k`=0, `t`≤0 ou `janela` NULL → no-op. Retorna nº de
 * substituições (telemetria).
 */
uint32_t janela_hampel(janela_t *janela, int k, float t);

/* RMS = √(média(x²)), acumulação em double. NULL/n=0 → 0.0f. */
float calcular_rms(const float *amostras, size_t n_amostras);

/* Variante com passo (stride) — lê um eixo do layout intercalado
 * [amostra][eixo] sem copiar. Mesmos defensivos + passo=0 → 0.0f. */
float calcular_rms_passo(const float *amostras, size_t n_amostras, size_t passo);

/* Kurtosis de Fisher (excesso) = m4/m2² − 3, momentos viesados (÷ n),
 * acumulação em double. Variância 0 → 0.0f. NULL/n=0 → 0.0f. */
float calcular_kurtosis(const float *amostras, size_t n_amostras);

/* Variante com passo (stride) da kurtosis — mesmo contrato da acima. */
float calcular_kurtosis_passo(const float *amostras, size_t n_amostras, size_t passo);

#ifdef __cplusplus
}
#endif
