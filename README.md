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
  signal_processing/       matemática pura do pipeline (RMS, FFT, kurtosis, THD)
  baseline/                calibração comandada (30 janelas, Welford) + NVS
  alert_manager/           classificação 3σ/6σ, votação/pior eixo, máquina de
                           estados, LED/buzzer (núcleo puro + serviço on-device)
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
intervalo de reporte do sensor, fila de janelas, LED/buzzer/botão de calibração).
Saída no serial: CSV de 6 métricas × 3 eixos por janela + logs de estado.

## Operação (calibração, classificação, LED/buzzer)

1. **Monitorar** `idf.py build flash monitor` — sem baseline, o nó aguarda em
   `BOOT` (janelas descartadas, LED apagado): *não há classificação sem baseline*.
2. **Calibrar** com o equipamento em regime saudável: digite `calibrar` (serial)
   ou pressione o botão BOOT. LED **azul piscando**; 30 janelas (~30 s) → baseline
   (média/σ por métrica e eixo, Welford) salvo em NVS → `MONITORANDO`.
   Reboot carrega o baseline da NVS e entra direto em `MONITORANDO`.
3. **Classificar**: cada janela é comparada ao baseline (atenção > média+3σ,
   crítico > média+6σ, por métrica); votação por eixo e **pior eixo** dão o estado
   do equipamento. LED: **verde** fixo / **amarelo** piscando / **vermelho** fixo;
   buzzer soa APENAS em vermelho (crítico). `status` imprime estado, baseline e
   uptime. `transitar()`/classificação são funções puras (SPEC, seams 1 e 2).

Demo (anomalia): com o nó monitorando, induza vibração (tocar/auxiliar na carcaça,
peso no cooler) → LED amarelo → vermelho + buzzer; remova o estímulo → volta a verde.

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

| Sinalização | ESP32-S3 | Nota |
|-------------|----------|------|
| LED RGB — canal R | GPIO 39 | LED RGB **externo**, resistor série ~220–330 Ω por canal |
| LED RGB — canal G | GPIO 40 | idem |
| LED RGB — canal B | GPIO 41 | idem |
| Buzzer ativo | GPIO 42 | nível liga/desliga; ajuste em menuconfig se o wiring diferir |
| Botão calibração | GPIO 0 | botão **BOOT** onboard, ativo baixo, pull-up interno |

LED RGB: catodo comum por padrão (canal acende com nível alto — `menuconfig →
PulsoPNAAT → Sinalização`); para anodo comum, desmarque "Canais do LED ligam em
nível alto". Um canal com GPIO negativo fica desativado (ex.: LED de cor única).
No Heltec V3 evite 8–14 (rádio LoRa) e 17/18/21 (OLED).

Driver do sensor: `rinku404/esp-idf-bno085` v1.2.0 (I2C apenas, HAL SH-2) —
dependência registrada em `main/idf_component.yml`.
