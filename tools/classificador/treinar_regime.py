"""Treina offline o reconhecedor de regime (parado, velocidade 1, 2, 3) com saúde por regime e gera o modelo embarcado.

Cada regime vira uma gaussiana nas 18 features log (as mesmas do detector de anomalia):
média própria e covariância com encolhimento Ledoit-Wolf. A janela vai para o regime de
menor d² + log|Σ|, e a distância a ele, comparada aos limiares daquele regime, diz se o
equipamento está saudável (verde), em atenção (amarelo) ou crítico (vermelho).

Uso:
  python tools/classificador/treinar_regime.py dataset/ventilador/serial_2026-09-16.csv \
    --segmentos dataset/ventilador/segmentos.csv --eventos dataset/ventilador/eventos_limiares_2026-09-16.txt --margem-s 8

Rótulos: pelo arquivo de segmentos (inicio,fim,classe,grupo em UTC) ou, sem ele, pela coluna
`rotulo` do CSV (parado/vel1/vel2/vel3/falha, gravada pelo firmware via RF15).
Classes: parado, vel1, vel2, vel3 e, se houver trechos `falha` no grupo treino, a falha conhecida
(reconhecê-la é crítico). Trechos `falha` só no grupo teste avaliam a detecção sem treino.
"""
import argparse
import csv
import datetime as dt
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import treinar_detector as td  # noqa: E402  (mesmas features e utilitários do detector)

RAIZ = Path(__file__).resolve().parents[2]
MODELO_PADRAO = RAIZ / "components" / "regime_classifier" / "modelo_regime.h"
RELATORIO_PADRAO = Path(__file__).resolve().parent / "relatorio_regime.md"

REGIMES = {"parado": 0, "vel1": 1, "vel2": 2, "vel3": 3}
FALHAS = {"falha": 4}  # REGIME_FALHA
CLASSES = {**REGIMES, **FALHAS}
NOMES = {0: "PARADO", 1: "VEL1", 2: "VEL2", 3: "VEL3", 4: "FALHA"}
MAX_CLASSES = 5
JANELAS_CONFIRMAR = 3   # CONFIG_PULSOPNAAT_ALERTA_JANELAS_CONFIRMAR
FILTRO_JANELAS = 9      # REGIME_FILTRO_JANELAS
FILTRO_MINIMO = 7       # REGIME_FILTRO_MINIMO
VERDE, AMARELO, VERMELHO = 0, 1, 2


def ler_instante(texto):
    return dt.datetime.fromisoformat(texto.strip().replace("Z", "+00:00"))


def carregar_janelas(entradas):
    arquivos = []
    for entrada in entradas:
        p = Path(entrada)
        arquivos += sorted(p.glob("*.csv")) if p.is_dir() else [p]
    janelas = []
    for arq in arquivos:
        if arq.stat().st_size == 0 or arq.name == "segmentos.csv":
            continue
        with arq.open(newline="", encoding="utf-8", errors="replace") as fh:
            for linha in csv.DictReader(fh):
                try:
                    instante = ler_instante(linha["timestamp"])
                    x = [td.transformar(col.split("_")[0], float(linha[col])) for col in td.COLUNAS]
                except (KeyError, TypeError, ValueError, AttributeError):
                    continue  # linha truncada, timestamp boot+s ou cabeçalho repetido
                limiares = linha.get("estado_limiares") or ""
                janelas.append({
                    "t": instante,
                    "x": x,
                    "rotulo": (linha.get("rotulo") or "sem_rotulo").strip(),
                    # Opinião dos limiares: bruta (firmware novo) ou já confirmada (firmware antigo).
                    "limiares_bruto": {"VERDE": VERDE, "AMARELO": AMARELO, "VERMELHO": VERMELHO}.get(limiares),
                    "limiares_confirmado": {"VERDE": VERDE, "AMARELO": AMARELO, "VERMELHO": VERMELHO}.get(
                        linha.get("estado_equipamento") or ""),
                })
    janelas.sort(key=lambda j: j["t"])
    return arquivos, janelas


def aplicar_eventos_limiares(janelas, caminho):
    """Preenche a opinião confirmada dos limiares a partir das linhas 'estado do equipamento: A → B'
    do log serial (firmware sem modelo), para janelas vindas de CSV sem essa coluna."""
    nivel = {"verde": VERDE, "amarelo": AMARELO, "vermelho": VERMELHO}
    trocas = []
    with Path(caminho).open(encoding="utf-8", errors="replace") as fh:
        for linha in fh:
            if "estado do equipamento:" not in linha:
                continue
            instante = ler_instante(linha.split(" ", 1)[0])
            antes, depois = linha.split("estado do equipamento:", 1)[1].split("→")
            trocas.append((instante, nivel[antes.split()[0]], nivel[depois.split()[0]]))
    if not trocas:
        return
    trocas.sort()
    i, estado = 0, trocas[0][1]
    for j in janelas:
        while i < len(trocas) and trocas[i][0] <= j["t"]:
            estado = trocas[i][2]
            i += 1
        if j["limiares_confirmado"] is None and j["limiares_bruto"] is None:
            j["limiares_confirmado"] = estado


def rotular(janelas, segmentos, margem_s, fracao_teste):
    """Atribui classe e grupo (treino/teste) a cada janela; devolve só as rotuladas."""
    rotuladas = []
    if segmentos:
        with Path(segmentos).open(newline="", encoding="utf-8") as fh:
            segs = [{
                "inicio": ler_instante(s["inicio"]) + dt.timedelta(seconds=margem_s),
                "fim": ler_instante(s["fim"]) - dt.timedelta(seconds=margem_s),
                "classe": s["classe"].strip(),
                "grupo": s["grupo"].strip(),
            } for s in csv.DictReader(fh)]
        for j in janelas:
            for s in segs:
                if s["inicio"] <= j["t"] <= s["fim"]:
                    rotuladas.append({**j, "classe": s["classe"], "grupo": s["grupo"]})
                    break
        return rotuladas

    # Sem segmentos: rótulo do firmware; teste = fração final (cronológica) de cada classe.
    por_classe = {}
    for j in janelas:
        if j["rotulo"] in REGIMES or j["rotulo"] == "falha":
            por_classe.setdefault(j["rotulo"], []).append(j)
    for classe, lista in por_classe.items():
        corte = len(lista) if classe == "falha" else int(len(lista) * (1.0 - fracao_teste))
        for i, j in enumerate(lista):
            grupo = "teste" if (classe == "falha" or i >= corte) else "treino"
            rotuladas.append({**j, "classe": classe, "grupo": grupo})
    rotuladas.sort(key=lambda j: j["t"])
    return rotuladas


def treinar(janelas, covariancia):
    valores = sorted({CLASSES[j["classe"]] for j in janelas})
    x = np.array([j["x"] for j in janelas])
    media_global = x.mean(axis=0)
    escala = x.std(axis=0)
    escala[escala < 1e-6] = 1.0
    z = (x - media_global) / escala
    y = np.array([CLASSES[j["classe"]] for j in janelas])

    medias, precisoes, log_dets, shrinks = [], [], [], []
    if covariancia == "compartilhada":
        centrado = np.vstack([z[y == v] - z[y == v].mean(axis=0) for v in valores])
        cov, shrink = td.covariancia_ledoit_wolf(centrado)
    for v in valores:
        zk = z[y == v]
        if len(zk) < 2 * zk.shape[1]:
            sys.exit(f"ERRO: regime {NOMES[v]} tem só {len(zk)} janelas de treino (mínimo {2 * zk.shape[1]}).")
        medias.append(zk.mean(axis=0))
        if covariancia == "por_regime":
            cov, shrink = td.covariancia_ledoit_wolf(zk)
        precisoes.append(np.linalg.inv(cov))
        log_dets.append(float(np.linalg.slogdet(cov)[1]))
        shrinks.append(shrink)
    return {
        "valores": valores, "media_global": media_global, "escala": escala,
        "medias": np.array(medias), "precisoes": np.array(precisoes),
        "log_dets": np.array(log_dets), "shrinks": shrinks,
    }


def aplicar(modelo, janelas):
    """Devolve (regime por janela, distância ao regime vencedor) como o firmware calcula."""
    z = (np.array([j["x"] for j in janelas]) - modelo["media_global"]) / modelo["escala"]
    d2 = np.stack([
        np.maximum(np.einsum("ij,jk,ik->i", z - m, p, z - m), 0.0)
        for m, p in zip(modelo["medias"], modelo["precisoes"])
    ], axis=1)
    k = np.argmin(d2 + modelo["log_dets"], axis=1)
    return k, np.sqrt(d2[np.arange(len(k)), k])


def limiares_por_regime(modelo, janelas, quantil, fator_critico):
    k, dist = aplicar(modelo, janelas)
    verdade = np.array([modelo["valores"].index(CLASSES[j["classe"]]) for j in janelas])
    atencao, critico = [], []
    for i in range(len(modelo["valores"])):
        proprias = dist[(k == i) & (verdade == i)]
        limiar = float(np.quantile(proprias, quantil)) if len(proprias) else float(np.quantile(dist, quantil))
        atencao.append(limiar)
        critico.append(limiar * fator_critico)
    return np.array(atencao), np.array(critico)


def filtrar_moda(regimes):
    """Mesma histerese do regime_filtro_atualizar: troca só com FILTRO_MINIMO das últimas FILTRO_JANELAS."""
    saida, historico, atual = [], [], None
    for r in regimes:
        historico = (historico + [r])[-FILTRO_JANELAS:]
        melhor, contagem = r, 0
        for candidato in reversed(historico):  # empate: fica o mais recente
            c = historico.count(candidato)
            if c > contagem:
                melhor, contagem = candidato, c
        if atual is None or (melhor != atual and contagem >= FILTRO_MINIMO):
            atual = melhor
        saida.append(atual)
    return saida


def confirmar(classificados, k=JANELAS_CONFIRMAR):
    """Mesma persistência do alerta_servico: muda só após k janelas seguidas do mesmo candidato."""
    estado, candidato, seguidas, saida = VERDE, None, 0, []
    for c in classificados:
        if c == estado:
            candidato, seguidas = None, 0
        elif c == candidato:
            seguidas += 1
        else:
            candidato, seguidas = c, 1
        if candidato is not None and seguidas >= k:
            estado, candidato, seguidas = candidato, None, 0
        saida.append(estado)
    return saida


def por_segmento(janelas, valores):
    """Aplica uma função sequencial (filtro/confirmação) respeitando lacunas de tempo entre trechos."""
    blocos, atual = [], []
    for i, j in enumerate(janelas):
        if atual and (j["t"] - janelas[atual[-1]]["t"]).total_seconds() > 5:
            blocos.append(atual)
            atual = []
        atual.append(i)
    if atual:
        blocos.append(atual)
    saida = [None] * len(janelas)
    for bloco in blocos:
        for i, v in zip(bloco, valores([janelas[i] for i in bloco], bloco)):
            saida[i] = v
    return saida


def saude_matriz(verdade_falha, alerta):
    verdade = np.array(verdade_falha, dtype=bool)
    predito = np.array(alerta, dtype=bool)
    return td.matriz_confusao(verdade, predito)


def escrever_modelo(caminho, modelo, atencao, critico, decide, meta):
    n = len(modelo["valores"])
    zeros18 = [0.0] * len(td.COLUNAS)
    vetor = lambda v: "{" + ", ".join(td.literal_c(x) for x in v) + "}"
    medias = [modelo["medias"][i] if i < n else zeros18 for i in range(MAX_CLASSES)]
    precisoes = []
    for i in range(MAX_CLASSES):
        linhas = modelo["precisoes"][i] if i < n else [zeros18] * len(td.COLUNAS)
        precisoes.append("        {\n" + ",\n".join(f"            {vetor(l)}" for l in linhas) + "\n        }")
    completar = lambda v, fill: list(v) + [fill] * (MAX_CLASSES - n)
    bloco_medias = ",\n".join("        " + vetor(m) for m in medias)
    bloco_precisoes = ",\n".join(precisoes)
    caminho.write_text(
        f"""/* GERADO por tools/classificador/treinar_regime.py em {meta['quando']} — não editar à mão.
 * Regimes: {', '.join(NOMES[v] for v in modelo['valores'])}; {meta['n_treino']} janelas de treino de {meta['n_arquivos']} arquivo(s).
 * Covariância {meta['covariancia']} (Ledoit-Wolf); limiar de atenção = quantil {meta['quantil']} da distância
 * de cada regime; crítico = {meta['fator']}× atenção. Portão de decisão: {meta['portao']}.
 */
#pragma once

#include "regime_classifier.h"

#define MODELO_REGIME_DISPONIVEL 1

static const regime_modelo_t MODELO_REGIME = {{
    .n_classes = {n},
    .valor = {{{', '.join(str(v) for v in completar(modelo['valores'], -1))}}},
    .falha = {{{', '.join('true' if v in FALHAS.values() else 'false' for v in completar(modelo['valores'], -1))}}},
    .media_global = {vetor(modelo['media_global'])},
    .escala = {vetor(modelo['escala'])},
    .media = {{
{bloco_medias}
    }},
    .precisao = {{
{bloco_precisoes}
    }},
    .log_det = {vetor(completar(modelo['log_dets'], 0.0))},
    .limiar_atencao = {vetor(completar(atencao, 0.0))},
    .limiar_critico = {vetor(completar(critico, 0.0))},
    .decide_estado = {'true' if decide else 'false'},
}};
""",
        encoding="utf-8",
    )


def main():
    ap = argparse.ArgumentParser(description="Treina o reconhecedor de regime + saúde por regime (RF15).")
    ap.add_argument("entradas", nargs="+", help="pasta(s) com CSVs do cartão ou da serial, ou arquivos CSV")
    ap.add_argument("--segmentos", help="CSV inicio,fim,classe,grupo (UTC) que rotula os trechos")
    ap.add_argument("--eventos", help="log serial com 'estado do equipamento: A → B' para comparar com os limiares")
    ap.add_argument("--margem-s", type=float, default=10.0, help="segundos descartados nas bordas de cada trecho")
    ap.add_argument("--fracao-teste", type=float, default=0.3, help="sem segmentos: fração final de cada classe para teste")
    ap.add_argument("--covariancia", choices=["por_regime", "compartilhada"], default="por_regime")
    ap.add_argument("--quantil-atencao", type=float, default=0.999,
                    help="quantil da distância de treino de cada regime usado como limiar de atenção")
    ap.add_argument("--fator-critico", type=float, default=1.5)
    ap.add_argument("--saida-modelo", type=Path, default=MODELO_PADRAO)
    ap.add_argument("--relatorio", type=Path, default=RELATORIO_PADRAO)
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")  # console do Windows não é UTF-8 por padrão

    arquivos, janelas = carregar_janelas(args.entradas)
    if args.eventos:
        aplicar_eventos_limiares(janelas, args.eventos)
    rotuladas = rotular(janelas, args.segmentos, args.margem_s, args.fracao_teste)
    contagem = {}
    for j in rotuladas:
        contagem[(j["classe"], j["grupo"])] = contagem.get((j["classe"], j["grupo"]), 0) + 1
    print(f"{len(arquivos)} arquivo(s) | {len(janelas)} janelas | {len(rotuladas)} rotuladas")
    for (classe, grupo), n in sorted(contagem.items()):
        print(f"  {classe:>7} {grupo:>6}: {n}")

    treino = [j for j in rotuladas if j["grupo"] == "treino"]
    teste = [j for j in rotuladas if j["grupo"] == "teste"]
    teste_saudavel = [j for j in teste if j["classe"] in REGIMES]
    falhas = [j for j in teste if j["classe"] in FALHAS]
    if not any(j["classe"] in REGIMES for j in treino):
        sys.exit("ERRO: nenhuma janela saudável de treino rotulada.")
    usar_falha = any(j["classe"] in FALHAS for j in treino)

    # 1) Avaliação honesta: modelo treinado só no grupo treino, medido no grupo teste.
    modelo = treinar(treino, args.covariancia)
    atencao, critico = limiares_por_regime(modelo, treino, args.quantil_atencao, args.fator_critico)
    valores = modelo["valores"]

    linhas_conf, acc_regime, acc_regime_filtrado = [], math.nan, math.nan
    conhecidas = [j for j in teste if CLASSES[j["classe"]] in valores]
    if conhecidas:
        k, _ = aplicar(modelo, conhecidas)
        predito = [valores[i] for i in k]
        filtrado = por_segmento(conhecidas, lambda _, bloco: filtrar_moda([predito[i] for i in bloco]))
        verdade = [CLASSES[j["classe"]] for j in conhecidas]
        acc_regime = float(np.mean([p == v for p, v in zip(predito, verdade)]))
        acc_regime_filtrado = float(np.mean([p == v for p, v in zip(filtrado, verdade)]))
        linhas_conf = ["| real \\ reconhecido | " + " | ".join(NOMES[v] for v in valores) + " |",
                       "|---|" + "---|" * len(valores)]
        for v in sorted(set(verdade)):
            linha = [sum(1 for p, t in zip(filtrado, verdade) if t == v and p == w) for w in valores]
            linhas_conf.append(f"| {NOMES[v]} | " + " | ".join(str(c) for c in linha) + " |")

    mc_modelo = mc_limiares = None
    if teste_saudavel and falhas:
        avaliadas = sorted(teste_saudavel + falhas, key=lambda j: j["t"])
        k, dist = aplicar(modelo, avaliadas)
        bruto = [VERMELHO if valores[i] in FALHAS.values() or d > critico[i]
                 else AMARELO if d > atencao[i] else VERDE for i, d in zip(k, dist)]
        confirmado = por_segmento(avaliadas, lambda _, bloco: confirmar([bruto[i] for i in bloco]))
        verdade_falha = [j["classe"] == "falha" for j in avaliadas]
        mc_modelo = saude_matriz(verdade_falha, [e != VERDE for e in confirmado])
        # Opinião dos limiares por trecho: bruta confirmada aqui (firmware novo) ou já confirmada no nó.
        def limiares_trecho(js, _):
            if all(j["limiares_bruto"] is not None for j in js):
                return confirmar([j["limiares_bruto"] for j in js])
            return [j["limiares_confirmado"] for j in js]
        lim = por_segmento(avaliadas, limiares_trecho)
        if all(e is not None for e in lim):
            mc_limiares = saude_matriz(verdade_falha, [e != VERDE for e in lim])

    if mc_modelo is None:
        portao, decide = "INCONCLUSIVO — faltam janelas de teste saudáveis ou de falha; modelo fica em modo sombra", False
    else:
        atinge = (acc_regime_filtrado >= td.META_ACURACIA and mc_modelo["acuracia"] >= td.META_ACURACIA
                  and mc_modelo["recall"] >= td.META_RECALL)
        supera = mc_limiares is None or mc_modelo["acuracia"] > mc_limiares["acuracia"]
        decide = atinge and supera
        if decide:
            portao = "APROVADO — atinge a meta da §6 e supera os limiares: o modelo decide LED, buzzer e alertas"
        elif atinge:
            portao = "meta da §6 atingida, mas sem superar os limiares — modelo fica em modo sombra"
        else:
            portao = (f"REPROVADO — abaixo da meta da §6 (regime {acc_regime_filtrado:.0%}, saúde "
                      f"{mc_modelo['acuracia']:.0%}, recall {mc_modelo['recall']:.0%}); modelo fica em modo sombra")

    # 2) Modelo embarcado: mesmo procedimento com todas as janelas rotuladas (treino + teste);
    #    a falha só vira classe se já estava no treino da avaliação.
    todas = [j for j in rotuladas if j["classe"] in REGIMES or (usar_falha and j["classe"] in FALHAS)]
    final = treinar(todas, args.covariancia)
    atencao_f, critico_f = limiares_por_regime(final, todas, args.quantil_atencao, args.fator_critico)
    quando = dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    escrever_modelo(args.saida_modelo, final, atencao_f, critico_f, decide, {
        "quando": quando, "n_treino": len(todas), "n_arquivos": len(arquivos),
        "covariancia": args.covariancia, "quantil": args.quantil_atencao, "fator": args.fator_critico,
        "portao": portao.split(" — ")[0],
    })

    pct = lambda v: "—" if isinstance(v, float) and math.isnan(v) else f"{v * 100:.1f}%"
    rel = [
        "# Relatório do reconhecedor de regime e saúde por regime",
        "",
        f"Gerado em {quando} a partir de {len(arquivos)} arquivo(s). Covariância {args.covariancia}; "
        f"margem de {args.margem_s:.0f} s nas bordas de cada trecho.",
        "",
        "| Classe | Treino | Teste |",
        "|---|---|---|",
    ]
    for classe in list(REGIMES) + ["falha"]:
        rel.append(f"| {classe} | {contagem.get((classe, 'treino'), 0)} | {contagem.get((classe, 'teste'), 0)} |")
    rel += [
        "",
        "## Condição reconhecida (janelas de teste de classes conhecidas pelo modelo)",
        "",
        f"- Acurácia por janela: {pct(acc_regime)}",
        f"- Acurácia após o filtro do firmware ({FILTRO_MINIMO} de {FILTRO_JANELAS} janelas para trocar): {pct(acc_regime_filtrado)}",
        "",
        *linhas_conf,
        "",
        "## Saúde (teste saudável + falha, com confirmação de 3 janelas)",
        "",
        "| Método | VP | VN | FP | FN | Acurácia | Taxa FP | Recall |",
        "|---|---|---|---|---|---|---|---|",
    ]
    if mc_modelo:
        rel.append(td.tabela("Modelo por regime", mc_modelo))
    if mc_limiares:
        rel.append(td.tabela("Limiares média+kσ (calibração única)", mc_limiares))
    rel += [
        "",
        "Limiares do modelo embarcado (distância de Mahalanobis):",
        "",
        "| Regime | Atenção | Crítico |",
        "|---|---|---|",
        *[f"| {NOMES[v]} | {a:.3f} | {c:.3f} |" for v, a, c in zip(final['valores'], atencao_f, critico_f)],
        "",
        f"**Portão de decisão (§5.4):** {portao}",
        "",
        "Métricas medidas com o modelo treinado só no grupo `treino` e avaliado em visitas separadas "
        "(grupo `teste`); o modelo embarcado repete o treino com todas as janelas rotuladas.",
        "",
    ]
    args.relatorio.write_text("\n".join(rel), encoding="utf-8")

    print(f"regime: acurácia {pct(acc_regime)} (filtrado {pct(acc_regime_filtrado)})")
    print("\n".join(linhas_conf))
    if mc_modelo:
        print(td.tabela("Modelo", mc_modelo))
    if mc_limiares:
        print(td.tabela("Limiares", mc_limiares))
    print(f"Portão: {portao}")
    print(f"Modelo: {args.saida_modelo}\nRelatório: {args.relatorio}")


if __name__ == "__main__":
    main()
