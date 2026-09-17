# Relatório do reconhecedor de regime e saúde por regime

Gerado em 2026-09-16 19:28 a partir de 2 arquivo(s). Covariância por_regime; margem de 8 s nas bordas de cada trecho.

| Classe | Treino | Teste |
|---|---|---|
| parado | 621 | 167 |
| vel1 | 265 | 183 |
| vel2 | 610 | 334 |
| vel3 | 296 | 183 |
| falha | 806 | 223 |

## Condição reconhecida (janelas de teste de classes conhecidas pelo modelo)

- Acurácia por janela: 79.3%
- Acurácia após o filtro do firmware (7 de 9 janelas para trocar): 90.0%

| real \ reconhecido | PARADO | VEL1 | VEL2 | VEL3 | FALHA |
|---|---|---|---|---|---|
| PARADO | 167 | 0 | 0 | 0 | 0 |
| VEL1 | 0 | 149 | 34 | 0 | 0 |
| VEL2 | 0 | 0 | 259 | 75 | 0 |
| VEL3 | 0 | 0 | 0 | 183 | 0 |
| FALHA | 0 | 0 | 0 | 0 | 223 |

## Saúde (teste saudável + falha, com confirmação de 3 janelas)

| Método | VP | VN | FP | FN | Acurácia | Taxa FP | Recall |
|---|---|---|---|---|---|---|---|
| Modelo por regime | 219 | 861 | 6 | 4 | 99.1% | 0.7% | 98.2% |
| Limiares média+kσ (calibração única) | 163 | 69 | 798 | 60 | 21.3% | 92.0% | 73.1% |

Limiares do modelo embarcado (distância de Mahalanobis):

| Regime | Atenção | Crítico |
|---|---|---|
| PARADO | 10.807 | 16.210 |
| VEL1 | 6.368 | 9.552 |
| VEL2 | 7.613 | 11.419 |
| VEL3 | 6.489 | 9.733 |
| FALHA | 7.610 | 11.415 |

**Portão de decisão (§5.4):** APROVADO — atinge a meta da §6 e supera os limiares: o modelo decide LED, buzzer e alertas

Métricas medidas com o modelo treinado só no grupo `treino` e avaliado em visitas separadas (grupo `teste`); o modelo embarcado repete o treino com todas as janelas rotuladas.
