/*
 * signal_processing — matemática de sinais por janela, por eixo (PulsoPNAAT).
 *
 * Componente determinístico e testável isoladamente do hardware: sem
 * FreeRTOS, sem I/O, sem alocação e sem estado mutável — os mesmos fontes
 * compilam no alvo e no PC, o que permite testar a matemática do pipeline
 * com entradas sintéticas e saídas esperadas conhecidas (validação on-target
 * via test_app). A única dependência externa é o esp-dsp (FFT de Hann), uma
 * biblioteca de CÁLCULO puro otimizada para o ESP32-S3 — não toca hardware
 * nem SO; suas tabelas são constantes inicializadas por
 * `signal_processing_init()` no boot.
 *
 * Cadeia de métricas por eixo, a cada janela de 1 s:
 *   RMS → nível geral de vibração (m/s²)
 *   Amplitude harmônica 1x (= f0) e 2x (= 2·f0) → desbalanceamento,
 *       desalinhamento (via FFT com janela de Hann)
 *   Banda 3x–5x (amplitude máxima em [3·f0, 5·f0]) → folga mecânica
 *   Kurtosis (excesso de Fisher) → impulsividade no domínio do tempo
 *   THD (sobre harmônicos 1x–5x) → degradação espectral geral
 *
 * f0 = RPM_nominal/60 vem da configuração de boot (Kconfig) e é passado como
 * parâmetro — a função permanece pura, sem ler configuração nem relógio.
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
#define JANELA_FS_NOMINAL_HZ 500.0f

/*
 * FFT: as n amostras da janela (com Hann) recebem zero-padding para 512
 * pontos (potência de 2 exigida pelo esp-dsp). Com fs = 500 Hz, resolução de
 * bin = 500/512 ≈ 0,98 Hz e Nyquist = 250 Hz (requisitos v2.1, RF03).
 */
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
 *
 * `rms`, `harmonica_1x`, `harmonica_2x` e `banda_3x_5x` em m/s² (amplitudes
 * físicas, normalizadas pelo ganho coerente da janela de Hann aplicada);
 * `kurtosis` e `thd` adimensionais.
 */
typedef struct {
    /* RMS = √(média(x²)) — inclui DC (gravidade em repouso ≈ 9,81 m/s²). */
    float rms[JANELA_NUM_EIXOS];
    /* Amplitude espectral no bin mais próximo de f0 (1x) e de 2·f0 (2x). */
    float harmonica_1x[JANELA_NUM_EIXOS];
    float harmonica_2x[JANELA_NUM_EIXOS];
    /* Amplitude máxima na banda [3·f0, 5·f0]. */
    float banda_3x_5x[JANELA_NUM_EIXOS];
    /*
     * Kurtosis amostral de Fisher (excesso): m4/m2² − 3 com momentos
     * centrais viesados (divisão por n). Gaussiana → 0; senoide pura → −1,5;
     * impulsiva → positivo e alto. Sinal constante (variância 0) → 0.
     */
    float kurtosis[JANELA_NUM_EIXOS];
    /*
     * THD = √(V₂² + V₃² + V₄² + V₅²) / V₁ sobre os harmônicos 1x–5x.
     * Definido como 0 quando a fundamental não se destaca do ruído
     * (V₁ ≤ 10⁻³·RMS) — sem a guarda, janelas sem 1x produzem THD
     * explosivo (razão entre dois vazamentos de FFT), inútil para a
     * classificação futura por baseline.
     */
    float thd[JANELA_NUM_EIXOS];
} metricas_t;

/*
 * Inicialização do componente — chamar UMA VEZ no boot (app_main ou runner
 * de testes) antes de qualquer `analisar_janela`. Idempotente: constrói as
 * tabelas de twiddle do esp-dsp (dsps_fft2r_init_fc32, padrão dos exemplos
 * oficiais). Sem a inicialização, a FFT retorna erro e as métricas
 * espectrais saem zeradas (RMS e kurtosis continuam válidas).
 */
void signal_processing_init(void);

/*
 * Função de análise da janela — PURA e testável no host/alvo.
 *
 * Toda a matemática do pipeline (RMS, Hann + FFT + harmônicos, kurtosis,
 * THD e, nas próximas fases, classificação contra o baseline) vive aqui,
 * para que seja verificável isoladamente do hardware. Sem alocação, I/O ou
 * estado mutável. NÃO reentrante: usa buffers de rascunho estáticos
 * (totalmente reescritos a cada chamada) — consumo por uma única task de
 * processamento, conforme a arquitetura da SPEC.
 *
 * `f0_hz` — frequência fundamental de rotação = RPM_nominal/60 (config de
 * boot). O mapeamento de bins usa a fs EFETIVA da janela, medida pelos
 * timestamps SH-2 (SPEC, seção Taxa/janela/FFT), com fallback para a nominal
 * (500 Hz) quando os timestamps não servirem. `f0_hz ≤ 0` → métricas
 * espectrais zeradas (RMS e kurtosis ainda calculadas).
 *
 * `janela` NULL → `metricas_out` recebe zeros (defensivo, não é caso normal).
 * `metricas_out` NULL → no-op. `n_amostras` > JANELA_N_AMOSTRAS → limitado.
 */
void analisar_janela(const janela_t *janela, float f0_hz, metricas_t *metricas_out);

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

/*
 * Kurtosis amostral de Fisher (excesso) = m4/m2² − 3, com momentos centrais
 * viesados (divisão por n) e acumulação em double. Variância 0 (sinal
 * constante) → 0.0f. NULL/n=0 → 0.0f.
 */
float calcular_kurtosis(const float *amostras, size_t n_amostras);

/*
 * Variante com passo (stride) da kurtosis — mesmo contrato de
 * calcular_kurtosis; usada por analisar_janela no layout intercalado
 * [amostra][eixo] sem copiar dados.
 */
float calcular_kurtosis_passo(const float *amostras, size_t n_amostras, size_t passo);

#ifdef __cplusplus
}
#endif
