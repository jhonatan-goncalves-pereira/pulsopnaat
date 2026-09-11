/*
 * storage — data logging em microSD com timestamp do RTC DS3231
 * (PulsoPNAAT, RF12/RNF09).
 *
 * Monta o cartão via SPI (FATFS) e cria uma task DEDICADA de baixa
 * prioridade, alimentada por fila, para a escrita — RNF09: a amostragem e o
 * processamento (DSP) NUNCA bloqueiam em I/O de cartão. `storage_log_janela`
 * só enfileira (cópia de struct, O(1)) e retorna; quem grava e lê o RTC é a
 * task interna.
 *
 * Rotação por tempo (CONFIG_PULSOPNAAT_LOG_ROTACAO_MIN): a cada janela de N
 * minutos um novo arquivo CSV é aberto. Isso NÃO substitui o requisito de
 * log persistente (RF12 — append, sobrevive a reboot): a sequência de
 * arquivos no cartão É o histórico completo; a rotação só limita o tamanho
 * de CADA arquivo, o que torna a importação em ferramentas como Grafana
 * (datasource CSV/Infinity) prática — sem reprocessar o cartão inteiro a
 * cada consulta.
 *
 * Poda por capacidade (CONFIG_PULSOPNAAT_LOG_ESPACO_MINIMO_KB): rede de
 * segurança, não é retenção por padrão. Só apaga os logs mais antigos se o
 * espaço livre do cartão cair abaixo do limite configurado — sem isso, um
 * cartão real eventualmente enche e trava toda escrita futura (RNF09).
 *
 * Degradação (RF12/RNF09): falha de montagem do cartão OU ausência do RTC
 * NÃO é erro fatal. storage_init() sempre retorna ESP_OK se a task/fila
 * foram criadas — mesmo sem cartão, mesmo sem RTC (nesse caso o timestamp
 * cai para "boot+<segundos>", relativo à ligação do nó). storage_disponivel()
 * informa o estado; storage_log_janela() vira no-op contado quando o cartão
 * não está montado.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#include "alert_manager.h"     /* estado_maquina_t, estado_equipamento_t */
#include "signal_processing.h" /* metricas_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    estado_maquina_t estado_maquina;
    estado_equipamento_t estado_equipamento;
    metricas_t metricas; /* 18 valores: 6 métricas × 3 eixos */
} storage_registro_t;

/*
 * Monta o microSD (pinos de CONFIG_PULSOPNAAT_SD_SPI_*), adiciona o DS3231
 * ao barramento I2C já criado (`bus_handle`, de i2c_config_init) e cria a
 * task de escrita. Chamar UMA VEZ, depois de i2c_config_init() e antes do
 * primeiro storage_log_janela().
 *
 * Retorna ESP_OK mesmo com cartão/RTC ausentes (degradação silenciosa,
 * logada); só falha (ESP_FAIL/ESP_ERR_NO_MEM) se a fila ou a task não
 * puderem ser criadas — nesse caso storage_log_janela() é seguro chamar,
 * mas não fará nada.
 */
esp_err_t storage_init(i2c_master_bus_handle_t bus_handle);

/* true se o cartão está montado e a task de escrita operacional agora. */
bool storage_disponivel(void);

/*
 * Enfileira um registro para gravação em CSV (não-bloqueante — RNF02/RNF09
 * por analogia ao padrão já usado para alertas). Fila cheia ou storage
 * indisponível → descarta e conta (log periódico agregado, não por item).
 * Seguro chamar de qualquer task; não é ISR-safe.
 */
void storage_log_janela(const storage_registro_t *reg);

#ifdef __cplusplus
}
#endif
