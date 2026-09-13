# Stack de observabilidade: MQTT → Telegraf → InfluxDB → Grafana

Bancada local para visualizar os alertas do PulsoPNAAT sem depender do
firmware: o nó publica em `pulsopnaat/#`, o Telegraf converte o JSON em
pontos do InfluxDB e o Grafana plota.

## Subir tudo

```bash
cp .env.example .env   # uma vez por máquina (o .env não vai para o Git)
docker compose up -d
```

Sobe:

- Mosquitto em `localhost:1883` (MQTT) e `localhost:9001` (websockets)
- InfluxDB 2.x em `localhost:8086` (org/bucket/token vêm do `.env`)
- Telegraf, inscrito em `pulsopnaat/status` e `pulsopnaat/alert`,
  gravando cada mensagem no InfluxDB
- Grafana em `localhost:3000` (datasource InfluxDB já provisionado via
  `grafana/provisioning/datasources/influxdb.yml`)

## Apontar o firmware para cá

No `idf.py menuconfig` → `MQTT Client → Broker URI`, aponte para o IP da
máquina que roda o compose (ex.: `mqtt://192.168.1.50:1883`). O
`mqtt://localhost:1883` do `sdkconfig.defaults` só vale quando o broker
roda na mesma máquina do teste.

## Tópicos e payloads reais

Contrato em `components/mqtt_client/include/mqtt_payloads.h` (nó → broker):

```
pulsopnaat/alert    {"node":"pulsopnaat-01","ts_us":123456789,
                     "estado":"amarelo (atenção)",
                     "rms_x":0.1234,"rms_y":0.2345,"rms_z":0.3456}
pulsopnaat/status   {"node":"pulsopnaat-01","maquina":"MONITORANDO",
                     "equipamento":"verde (normal)","baseline":true,"uptime_s":3600}
```

Direção inversa (broker → nó): `pulsopnaat/command` com `calibrar` ou
`status`. O Telegraf consome só `status` e `alert`; `command` não é
persistido (é ordem, não telemetria).

No InfluxDB isso vira as medições `pulsopnaat_status` e `pulsopnaat_alert`
(`name_override` no `telegraf.conf`), com `node` como tag, dá para
filtrar/agrupar por equipamento no Grafana. Strings (`maquina`,
`equipamento`, `estado`) são preservadas via `json_string_fields`; sem
isso o parser JSON do Telegraf descartaria os valores não numéricos em
silêncio.

## Conectar o Grafana

1. Acesse `http://localhost:3000` (usuário/senha do `.env`: `admin`/`admin`
   por padrão; o primeiro login pede troca de senha).
2. O datasource InfluxDB (Flux, `http://influxdb:8086`, uid
   `pulsopnaat-influxdb`) já vem provisionado, nada a configurar. Se ele
   aparecer sem credenciais, recrie o container (`docker compose up -d`;
   a troca de env recria e re-provisiona sozinha).
3. O dashboard PNAAT já vem em
   `grafana/provisioning/dashboards/json/` e aparece na pasta
   PulsoPNAAT. Para adicionar outro, salve o JSON na mesma pasta e
   reinicie o container (`docker compose restart grafana`).

## Teste rápido sem o firmware

Publica um status e um alerta de mentira direto no broker (de dentro do
container do Mosquitto) e confere no Grafana em ~10 s (flush do Telegraf):

```bash
docker exec mosquitto mosquitto_pub -t pulsopnaat/status \
  -m '{"node":"bancada-01","maquina":"MONITORANDO","equipamento":"verde (normal)","baseline":true,"uptime_s":42}'
docker exec mosquitto mosquitto_pub -t pulsopnaat/alert \
  -m '{"node":"bancada-01","ts_us":123456789,"estado":"vermelho (crítico)","rms_x":1.2345,"rms_y":0.2345,"rms_z":0.3456}'
```

## Ajustes e limites

- `allow_anonymous true` no `mosquitto/config/mosquitto.conf` e os segredos
  padrão do `.env.example` valem só para bancada local: use usuário/senha
  (ou TLS) no MQTT e segredos novos no `.env` antes de expor em qualquer rede.
- `interval`/`flush_interval` no `telegraf.conf` controlam a cadência de
  ingestão (padrão 10 s).
- Derrubar tudo (mantendo os dados): `docker compose down`. Apagar os dados
  também: `docker compose down -v`.

## Espelhando em nuvem/VPS

O mesmo compose sobe igual numa VPS (é só Docker): o firmware aponta o
Broker URI para o IP/domínio dela e o pipeline continua idêntico. O que
muda em relação à bancada, no mínimo: segredos novos no `.env`, MQTT com
usuário/senha (ou TLS) em vez de `allow_anonymous`, firewall liberando só
1883/3000 para quem precisa (InfluxDB 8086 fica interno) e backup dos
volumes `influxdb-data`/`grafana-data`. Sem isso, é bancada com IP
público, não produção.
