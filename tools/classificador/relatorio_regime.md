# Relatório do reconhecedor de regime e saúde por regime

Gerado em 2026-09-16 18:03 a partir de 1 arquivo(s). Covariância por_regime; margem de 8 s nas bordas de cada trecho.

| Classe | Treino | Teste |
|---|---|---|
| parado | 431 | 107 |
| vel1 | 210 | 97 |
| vel2 | 527 | 264 |
| vel3 | 224 | 114 |
| falha | 578 | 174 |

## Condição reconhecida (janelas de teste de classes conhecidas pelo modelo)

- Acurácia por janela: 84.4%
- Acurácia após o filtro do firmware (7 de 9 janelas para trocar): 90.2%

| real \ reconhecido | PARADO | VEL1 | VEL2 | VEL3 | FALHA |
|---|---|---|---|---|---|
| PARADO | 107 | 0 | 0 | 0 | 0 |
| VEL1 | 0 | 97 | 0 | 0 | 0 |
| VEL2 | 0 | 17 | 190 | 57 | 0 |
| VEL3 | 0 | 0 | 0 | 114 | 0 |
| FALHA | 0 | 0 | 0 | 0 | 174 |

## Saúde (teste saudável + falha, com confirmação de 3 janelas)

| Método | VP | VN | FP | FN | Acurácia | Taxa FP | Recall |
|---|---|---|---|---|---|---|---|
| Modelo por regime | 172 | 559 | 23 | 2 | 96.7% | 4.0% | 98.9% |
| Limiares média+kσ (calibração única) | 153 | 35 | 547 | 21 | 24.9% | 94.0% | 87.9% |

Limiares do modelo embarcado (distância de Mahalanobis):

| Regime | Atenção | Crítico |
|---|---|---|
| PARADO | 11.947 | 17.920 |
| VEL1 | 6.463 | 9.694 |
| VEL2 | 6.906 | 10.360 |
| VEL3 | 6.355 | 9.532 |
| FALHA | 7.840 | 11.760 |

**Portão de decisão (§5.4):** APROVADO — atinge a meta da §6 e supera os limiares: o modelo decide LED, buzzer e alertas

Métricas medidas com o modelo treinado só no grupo `treino` e avaliado em visitas separadas (grupo `teste`); o modelo embarcado repete o treino com todas as janelas rotuladas.
