# PulsoPNAAT

Sistema embarcado de **manutenção preditiva por análise de vibração**: um nó de
borda (ESP32-S3 + BNO085) fixado à carcaça de um equipamento rotativo amostra a
aceleração triaxial a ~500 Hz, calcula métricas por janela de 1 s (RMS hoje;
FFT, kurtosis e THD nas próximas fases), classifica o estado contra um baseline
calibrado (normal / atenção / crítico) e emite alertas via MQTT + LED/buzzer.

Documentos normativos: `requisitos.md` (v2.1) e `SPEC.md` (derivação técnica).

## Estrutura

```
main/                      app_main: fila, tasks com pinning, wiring dos componentes
components/
  i2c_config/              barramento I2C do BNO085
  vibration_sensor/        aquisição ACCELEROMETER (m/s²) @500 Hz, janelamento
  signal_processing/       matemática pura do pipeline (RMS hoje) + test/test_*.c
test_app/                  app de teste on-target (Unity no ESP32-S3)
requisitos.md, SPEC.md     normativos
```

## Build & Flash

Requer ESP-IDF ≥ 5.5 (`​. $IDF_PATH/export.sh`):

```bash
idf.py set-target esp32s3   # uma vez
idf.py build
idf.py flash monitor
```

Configuração do nó em `idf.py menuconfig` → **PulsoPNAAT** (pinos I2C/GPIO,
intervalo de reporte do sensor, fila de janelas). Saída atual: CSV de RMS por
eixo (m/s²) no serial, uma linha por janela.

## Testes (Unity on-target)

Os testes vivem em `components/<comp>/test/` e rodam na placa pelo app de teste
(mesmo componente `unity` do ESP-IDF, versão 2.6.0):

```bash
idf.py -C test_app build flash monitor
# resumo Unity no serial: "N Tests 0 Failures 0 Ignored / OK"
```

Nova suíte: criar `components/<comp>/test/test_<comp>.c` (API core do Unity:
`TEST_ASSERT_*`, função `rodar_testes_<comp>()`) e registrá-la no runner
`test_app/main/test_app_main.c`.

## Hardware (wiring Heltec LoRa V3)

| BNO085 | ESP32-S3 | Sinal |
|--------|----------|-------|
| SDA | GPIO 6 | I2C data |
| SCL | GPIO 7 | I2C clock |
| INT (H_INTN) | GPIO 5 | data-ready, ativo baixo |
| RST (NRST) | GPIO 4 | reset, ativo baixo |
| AD0 | GND | endereço I2C 0x28 (VCC → 0x29) |
| PS0/PS1 | GND | modo I2C (obrigatório) |
| VCC/GND | 3V3/GND | alimentação |

Driver do sensor: `rinku404/esp-idf-bno085` v1.2.0 (I2C apenas, HAL SH-2) —
dependência registrada em `main/idf_component.yml`.
