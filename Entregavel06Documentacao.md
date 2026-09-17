<p align="center">
  <img src="docs/img/pulsopnaat_cabecalho.png" alt="FIT · PulsoPNAAT — Sistema Embarcado de Manutenção Preditiva por Análise de Vibração" width="100%">
</p>

# Entregável 06: documentação do PulsoPNAAT

Grupo TCC 14 · PNAAT 2026

Equipe: Jetro Kepler Gomes Alencar Gonzaga Viana · Jhonatan Gonçalves Pereira ·
José Adiel Calixto Serafim · Lucas Vinicius Santos Leonel

**Repositório final:** <https://github.com/jhonatan-goncalves-pereira/pulsopnaat>

**Manual de replicação:** [README.md](README.md)

## Resumo

O PulsoPNAAT é um nó de borda (ESP32-S3 Heltec WiFi LoRa 32 V3 com acelerômetro GY-BNO085) preso na
carcaça de um equipamento rotativo. Ele mede a vibração a 400 Hz, calcula 18 métricas por segundo e
usa um modelo treinado com dados reais do próprio equipamento para reconhecer a condição (parado,
velocidade 1, 2 ou 3, ou falha de alimentação) e a saúde. O resultado sai no LED RGB e no buzzer, vai
por MQTT para um dashboard no Grafana e fica gravado em CSV no microSD, com a hora do RTC DS3231.

## Onde está cada item exigido

| Item da Entrega 6 | Onde está |
|---|---|
| Código-fonte | `main/`, `components/` (um componente por função), `test_app/` e `tools/`. Organização explicada no [README §3](README.md#3-estrutura-do-repositório) |
| Esquemáticos elétricos | [README §7.1](README.md#71-esquema): esquema em Mermaid e tabela de pinos. Imagens em [docs/img/pulsopnaat_pinout.svg](docs/img/pulsopnaat_pinout.svg) e [docs/img/PulsoPNAAT_protoboard.svg](docs/img/PulsoPNAAT_protoboard.svg) |
| Diagramas de arquitetura atualizados | [README §2](README.md#2-arquitetura): fluxo entrada → processamento → saída, diagrama da arquitetura (nó, broker, Telegraf, InfluxDB e Grafana), tarefas do firmware e máquina de estados |
| Pré-requisitos e recursos | [README §4](README.md#4-pré-requisitos): hardware, software e rede |
| Dependências e instalação | [README §5](README.md#5-dependências-e-instalação): tabela de dependências com versões e comandos de instalação |
| Passos de configuração | [README §6](README.md#6-configuração): `menuconfig`, `.env` da stack, calibração e tópicos MQTT |
| Instruções de montagem | [README §7](README.md#7-montagem-elétrica): esquema, tabela de pinos e passo a passo |
| Comandos para executar | [README §8](README.md#8-como-executar): gravar o firmware, subir a stack, operar, treinar o modelo e rodar os testes |
| Resultado que confirma a execução | [README §9](README.md#9-resultado-esperado): mensagens do serial, LED e buzzer, painéis do dashboard, MQTT e cartão SD |
| Função das partes do código | Um componente por responsabilidade ([README §3](README.md#3-estrutura-do-repositório)) e um comentário no início de cada arquivo explicando sua função |

Material complementar:

- Dataset rotulado da bancada: [dataset/ventilador/](dataset/ventilador/README.md)
- Modelo treinado e resultados: [README §10](README.md#10-modelo-treinado-e-dataset) e
  [tools/classificador/relatorio_regime.md](tools/classificador/relatorio_regime.md)
- Testes na placa (102 testes, 0 falhas): [README §11](README.md#11-testes)
- Critérios de sucesso e resultados medidos: [README §12](README.md#12-critérios-de-sucesso-e-resultados-medidos)
- Limitações e pendências declaradas: [README §13](README.md#13-limitações-e-pendências)
- Solução de problemas: [README §14](README.md#14-solução-de-problemas)
- Detalhes da stack Docker: [README_DOCKER.md](README_DOCKER.md)

## Resultados principais

Medidos em passagens do ventilador que ficaram fora do treino:

| | Modelo treinado | Limiares da calibração |
|---|---|---|
| Acurácia da saúde | 99,1% | 21,3% |
| Falso alarme em operação normal | 0,7% | 92,0% |
| Falhas detectadas | 98,2% | 73,1% |
| Condição reconhecida (parado, vel1, vel2, vel3, falha) | 90,0% | — |

Latência de processamento: cerca de 11 ms por janela de 1 s.

## Replicação em resumo

1. Monte o hardware conforme o [README §7](README.md#7-montagem-elétrica).
2. Instale o ESP-IDF 5.5.5, o Docker e o `numpy` ([README §5](README.md#5-dependências-e-instalação)).
3. Configure Wi-Fi e broker no `menuconfig` e crie o `.env` ([README §6](README.md#6-configuração)).
4. Grave o firmware, suba a stack e calibre ([README §8](README.md#8-como-executar)).
5. Confira as mensagens do serial, o LED e o dashboard ([README §9](README.md#9-resultado-esperado)).
