# PulsoPNAAT — Sistema Embarcado de Manutenção Preditiva por Análise de Vibração

**TCC — Trabalho de Conclusão da Capacitação PNAAT 2026** (FIT - Instituto de Tecnologia | MCTI Futuro | Softex)

**Equipe:**
- Jetro Kepler Gomes Alencar Gonzaga Viana
- Jhonatan Gonçalves Pereira
- José Adiel Calixto Serafim
- Lucas Vinicius Santos Leonel

---

## 1. O que é este projeto?

Sistema embarcado de manutenção preditiva por análise de vibração (ESP32-S3 + BNO085) — TCC PNAAT 2026, cenário 8: falha de maquinário rotativo em manufatura pesada.

Cenário 8 do PNAAT 2026: falha repentina de maquinário rotativo (motores elétricos e rolamentos) por ausência de acompanhamento preditivo, em ambiente de manufatura pesada.

## 2. O que compõe a solução?

**Hardware:**
- Placa de processamento: ESP32-S3 (Heltec WiFi LoRa 32 V3)
- Sensor: acelerômetro triaxial BNO085 (módulo GY-BNO085), conectado via SPI
- Sinalização local: LED e buzzer *(a partir da entrega 04 — atualizar conforme progresso)*

**Software:**
- Firmware em C++ sobre Arduino Framework (FreeRTOS)
- Bibliotecas: `Adafruit BNO08x`, `Adafruit BusIO`, `arduinoFFT`
- Métricas calculadas por janela: RMS, FFT (1x/2x/3x-5x), Kurtosis, THD
- Conectividade: MQTT sobre Wi-Fi *(a partir da entrega 04/05 — atualizar conforme progresso)*

## 3. Pré-requisitos

- [PlatformIO](https://platformio.org/) (extensão VS Code ou CLI)
- [Driver USB-Serial CP210x](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads) (para reconhecer a placa Heltec via USB)
- Git

## 4. Dependências e instalação

As dependências de biblioteca já estão declaradas no `platformio.ini` e são baixadas automaticamente pelo PlatformIO no primeiro build — não é necessário instalar nada manualmente além do PlatformIO em si.

```bash
git clone https://github.com/jhonatan-goncalves-pereira/pulsopnaat.git
cd pulsopnaat
pio run
```

Se o comando `pio run` compilar sem erros, as dependências foram resolvidas corretamente.

## 5. Configuração

*(preencher conforme a entrega evoluir — por enquanto a PoC não depende de credenciais)*

Quando a camada Wi-Fi/MQTT for integrada, as credenciais **não** serão gravadas no código-fonte. Um arquivo `.env.example` indicará quais variáveis configurar, e o `.env` real (com os dados reais) não será versionado — ver `.gitignore`.

## 6. Instruções de montagem (conexões elétricas)

Ligação SPI entre o ESP32-S3 (Heltec WiFi LoRa 32 V3) e o módulo GY-BNO085:

| BNO085 | ESP32-S3 (Heltec V3) |
|---|---|
| VIN | 3V3 |
| GND | GND |
| SCL/SCK | GPIO 4 |
| SDA/MOSI | GPIO 5 |
| DI (MISO) | GPIO 3 |
| CS | GPIO 2 |
| INT | GPIO 6 |
| RST | GPIO 7 |

O sensor deve ser fixado **rigidamente** à carcaça do equipamento monitorado (acoplamento rígido é pré-condição para leitura de vibração confiável — ver RNF03 do Documento de Requisitos).

📎 Esquemático elétrico completo: [`docs/diagrama-pinagem.png`](docs/diagrama-pinagem.png)
📎 Diagrama de arquitetura (fluxo de dados): [`docs/diagrama-blocos.png`](docs/diagrama-blocos.png)

## 7. Como executar

```bash
pio run -t upload        # grava o firmware na placa
pio device monitor -b 115200   # abre o monitor serial
```

## 8. Resultado esperado (confirmação de execução)

Ao abrir o monitor serial, você deve ver:

1. A mensagem `BNO085 conectado via SPI!`
2. Durante os primeiros ~30s, o log `Calibrando (N/40)...` (calibração automática do baseline)
3. Após a calibração, uma linha por janela processada no formato:
```
   Proc: 1840 us | RMS:120.4 FFT1x:88.2 Kurt:0.15 THD:0.22 -> VERDE
```
4. Ao induzir desbalanceamento no motor de teste (peso fixado em uma pá), o estado deve migrar para `AMARELO` ou `VERMELHO` em poucos segundos.

Se esses 4 pontos ocorrerem, a execução foi bem-sucedida.

## Estado atual do projeto

| Entrega | Status | Tag |
|---|---|---|
| Entrega 1 — Documento de Requisitos | ✅ Concluída | `entrega-01` |
| Entrega 2 — PoC (lógica de detecção) | 🚧 Em andamento | `entrega-02` |
| Entrega 3 — Estruturação de repositório | ⏳ Pendente | — |
| Entrega 4 — Integração final | ⏳ Pendente | — |

**Escopo atual (PoC):** valida exclusivamente se a lógica RMS+FFT+Kurtosis+THD roda dentro da janela de amostragem sem travar o processador e diferencia sinal saudável de sinal com desbalanceamento induzido, usando dados reais de motor/cooler de teste. Wi-Fi, MQTT, LED/buzzer e dashboard estão **fora do escopo** desta fase (ver seção 2.3 — Limitations and Exclusions do Documento de Requisitos).

Resultados da validação: [`data/coleta-poc-entrega02.csv`](data/coleta-poc-entrega02.csv)

## Próxima etapa

Integração das camadas de conectividade (Wi-Fi/MQTT) e sinalização local (LED/buzzer), seguindo estratégia de Integração Bottom-Up, sem alterar a lógica de detecção já validada nesta PoC.

---

## Padrão de commits

Adotamos uma convenção simplificada inspirada em [Conventional Commits](https://www.conventionalcommits.org/), com um conjunto reduzido de tipos suficiente para o escopo deste projeto:

| Tipo | Emoji | Quando usar |
|---|---|---|
| `feat` | ✨ | Nova funcionalidade (ex: cálculo de FFT, publicação MQTT) |
| `fix` | 🐛 | Correção de bug ou comportamento incorreto |
| `docs` | 📚 | Mudança apenas em documentação (README, diagramas, comentários) |
| `test` | 🧪 | Criação ou ajuste de testes/coleta de dados de validação |
| `refactor` | ♻️ | Reorganização de código sem mudar comportamento |
| `perf` | ⚡ | Melhoria de desempenho (latência, uso de memória) |
| `chore` | 🔧 | Configuração de build, dependências, `.gitignore` etc. |

**Formato:** `<emoji> <tipo>: <descrição curta no imperativo>`

Exemplos:
```bash
git commit -m "✨ feat: adiciona cálculo de kurtosis por janela"
git commit -m "🐛 fix: corrige overflow no buffer de amostragem SPI"
git commit -m "📚 docs: atualiza README com instruções de montagem"
git commit -m "🧪 test: registra coleta de dados da PoC (10 janelas saudáveis)"
```

Regra prática: mensagem curta e direta na primeira linha; se precisar explicar o *porquê* da mudança, use o corpo do commit (linha em branco + parágrafo).

## Padrão de branches

| Branch | Uso |
|---|---|
| `main` | Sempre estável e executável. Cada entrega recebe uma tag (`entrega-01`, `entrega-02`...) |
| `feature/<escopo>` | Nova funcionalidade (ex: `feature/fft-metrics`, `feature/mqtt-publish`) |
| `fix/<escopo>` | Correção de bug (ex: `fix/spi-timeout`) |
| `poc/<escopo>` | Experimentos que podem não virar código definitivo (ex: `poc/baseline-calibracao`) |
| `docs/<escopo>` | Alterações apenas de documentação |

Ninguém commita direto na `main`. Toda mudança nasce em uma branch com o prefixo adequado e é mesclada via merge/PR.# PulsoPNAAT

Sistema embarcado de **manutenção preditiva por análise de vibração**: um nó de
borda (ESP32-S3 + BNO085) fixado à carcaça de um equipamento rotativo amostra a
aceleração triaxial a ~400 Hz (sem a gravidade, via aceleração linear), calcula
métricas por janela de 1 s (RMS hoje;
FFT, kurtosis e THD nas próximas fases), classifica o estado contra um baseline
calibrado (normal / atenção / crítico) e emite alertas via MQTT + LED/buzzer.

Documentos normativos: `requisitos.md` (v2.1) e `SPEC.md` (derivação técnica).

## Estrutura

```
main/                      app_main: fila, tasks com pinning, wiring dos componentes
components/
  i2c_config/              barramento I2C do BNO085
  vibration_sensor/        aquisição LINEAR_ACCELERATION (m/s², sem gravidade) @400 Hz, janelamento
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
