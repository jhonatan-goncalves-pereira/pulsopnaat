# PulsoPNAAT — Sistema Embarcado de Manutenção Preditiva por Análise de Vibração

## 1. O que é este projeto?
[Cole aqui o parágrafo da seção 2.1/2.2 do seu documento de requisitos]

## 2. O que compõe a solução?
- Placa: ESP32-S3 (Heltec WiFi LoRa 32 V3)
- Sensor: acelerômetro BNO085 (GY-BNO085), via SPI
- Firmware: C++ sobre Arduino Framework, bibliotecas Adafruit BNO08x e arduinoFFT
- (fase da PoC: sem Wi-Fi/MQTT — ver seção "Estado atual" abaixo)

## 3. Pré-requisitos
- PlatformIO (VS Code extension ou CLI)
- Driver USB-Serial da placa Heltec (CP210x)

## 4. Como rodar
\`\`\`bash
git clone <url-do-repo>
cd pulsopnaat
pio run -t upload
pio device monitor -b 115200
\`\`\`

## Estado atual (Entrega 2 — PoC)
Esta entrega valida exclusivamente a variável técnica mais arriscada do projeto:
a lógica RMS+FFT+Kurtosis+THD roda dentro da janela de amostragem sem travar
e diferencia sinal saudável de sinal com desbalanceamento induzido, usando
dados reais de um motor/cooler de teste. Wi-Fi, MQTT, LED/buzzer e dashboard
ficam fora do escopo desta fase (ver seção 2.3 do Documento de Requisitos).

Resultados da validação: ver `data/coleta-poc-entrega02.csv`.

## Próxima etapa
Integração das camadas de conectividade (Wi-Fi/MQTT) e sinalização local
(LED/buzzer), seguindo estratégia de Integração Bottom-Up.