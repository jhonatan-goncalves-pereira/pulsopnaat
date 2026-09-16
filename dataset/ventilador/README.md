# Dataset rotulado: ventilador de 3 velocidades (bancada PulsoPNAAT)

Coleta real feita em 16/09/2026 com o nó PulsoPNAAT (ESP32-S3 Heltec V3 + GY-BNO085) preso à
carcaça de um ventilador doméstico de 3 velocidades. É a base de treino e avaliação do
`regime_classifier`: reconhece se o ventilador está parado, em qual velocidade gira e se há falha
de alimentação.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `serial_2026-09-16.csv` | Uma linha por janela de 1 s (400 amostras a 400 Hz): `timestamp` UTC do PC na chegada da linha serial + as 18 métricas do firmware (`rms`, `h1x`, `h2x`, `b3x5`, `kurt`, `thd` nos eixos x, y, z). 3036 janelas, de 19:52:31 a 20:43:25 UTC |
| `segmentos.csv` | Trechos rotulados: `inicio`, `fim` (UTC), `classe`, `grupo` (`treino`/`teste`) e observação |
| `eventos_limiares_2026-09-16.txt` | Trocas de estado decididas pelos limiares média+kσ do firmware durante a coleta (baseline calibrado na velocidade 3); usado para comparar com o modelo |
| `marcas_operador_2026-09-16.csv` | Instante em que cada troca foi avisada pelo operador (chega alguns segundos depois da troca real) |

## Como os rótulos foram definidos

O operador trocava a velocidade e avisava. O instante exato de cada troca veio da mudança brusca
de RMS nos dados, e não do aviso. O treino descarta 8 s em cada borda de trecho (`--margem-s 8`),
para tirar aceleração e desaceleração do motor.

| Classe | Condição física |
|---|---|
| `parado` | Ventilador desligado |
| `vel1`, `vel2`, `vel3` | Ventilador na tomada boa, na velocidade indicada |
| `falha` | Ventilador ligado num filtro de linha com mau contato (velocidades 1, 2 e 3): o motor recebe pouca energia e vibra a um quarto do normal |

## Separação treino × teste

O teste usa **visitas diferentes** das de treino: o ventilador saiu da condição e voltou depois.
Janelas vizinhas no tempo nunca ficam em grupos diferentes. A falha na velocidade 2 fica inteira no
teste, então o modelo é avaliado numa falha que não viu no treino.

Condições reais registradas durante a coleta (observação em `segmentos.csv`):

- Às 20:03:55 o ventilador foi balançado na troca de velocidade, e a vibração no eixo Z mudou
  depois disso.
- Na última visita da velocidade 2 (20:38–20:43) a vibração caiu devagar por minutos, enquanto o
  ventilador assentava depois da troca de tomada. É o trecho mais difícil do teste.

## Reproduzir o treino e o relatório

```bash
python -m pip install numpy
python tools/classificador/treinar_regime.py dataset/ventilador/serial_2026-09-16.csv \
  --segmentos dataset/ventilador/segmentos.csv \
  --eventos dataset/ventilador/eventos_limiares_2026-09-16.txt --margem-s 8
```

O script gera `components/regime_classifier/modelo_regime.h` (modelo embarcado) e
`tools/classificador/relatorio_regime.md` (matriz de confusão, saúde e portão de decisão).
