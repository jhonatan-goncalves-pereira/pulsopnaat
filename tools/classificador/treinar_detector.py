"""Treina offline o detector de anomalia (Mahalanobis, §5.4) com os CSVs do cartão SD e gera o modelo embarcado.

Uso: python tools/classificador/treinar_detector.py F:/pulso [outro.csv ...]
"""
import argparse
import csv
import datetime as dt
import math
import sys
from pathlib import Path

import numpy as np

RAIZ = Path(__file__).resolve().parents[2]
MODELO_PADRAO = RAIZ / "components" / "anomaly_detector" / "modelo_anomalia.h"
RELATORIO_PADRAO = Path(__file__).resolve().parent / "relatorio_detector.md"

# Espelha components/anomaly_detector/include/anomaly_detector.h — mudar lá exige mudar aqui.
EPS_AMPLITUDE = 1e-3
EPS_THD = 1e-2
DESLOC_KURTOSIS = 3.0
METRICAS = ["rms", "h1x", "h2x", "b3x5", "kurt", "thd"]
EIXOS = ["x", "y", "z"]
COLUNAS = [f"{met}_{eixo}" for eixo in EIXOS for met in METRICAS]
ESTADOS_CLASSIFICANDO = {"MONITORANDO", "CONTINGENCIA"}
# §6: acurácia ≥ 85% e ≥ 8/10 janelas de falha detectadas.
META_ACURACIA = 0.85
META_RECALL = 0.80


def transformar(metrica, v):
    if metrica == "kurt":
        return math.log(v + DESLOC_KURTOSIS if (v + DESLOC_KURTOSIS) > 1e-3 else 1e-3)
    eps = EPS_THD if metrica == "thd" else EPS_AMPLITUDE
    return math.log((v if v > 0 else 0.0) + eps)


def carregar(entradas):
    arquivos = []
    for entrada in entradas:
        p = Path(entrada)
        arquivos += sorted(p.glob("*.csv")) if p.is_dir() else [p]

    janelas = []
    for arq in arquivos:
        if arq.stat().st_size == 0:
            continue
        with arq.open(newline="", encoding="utf-8", errors="replace") as fh:
            for linha in csv.DictReader(fh):
                if linha.get("estado_maquina") not in ESTADOS_CLASSIFICANDO:
                    continue
                try:
                    x = [transformar(col.split("_")[0], float(linha[col])) for col in COLUNAS]
                except (KeyError, TypeError, ValueError):
                    continue  # linha truncada por queda de energia
                janelas.append({
                    "x": x,
                    "rotulo": (linha.get("rotulo") or "sem_rotulo").strip(),
                    "firmware_alerta": linha.get("estado_equipamento") != "VERDE",
                })
    return arquivos, janelas


def covariancia_ledoit_wolf(z):
    n, p = z.shape
    z = z - z.mean(axis=0)
    emp = z.T @ z / n
    z2 = z ** 2
    traco_emp = z2.sum(axis=0) / n
    mu = traco_emp.sum() / p
    beta_ = np.sum(z2.T @ z2)
    delta_ = np.sum((z.T @ z) ** 2) / n ** 2
    beta = (beta_ / n - delta_) / (p * n)
    delta = (delta_ - 2.0 * mu * traco_emp.sum() + p * mu ** 2) / p
    beta = min(beta, delta)
    shrinkage = 0.0 if beta == 0 else beta / delta
    return (1.0 - shrinkage) * emp + shrinkage * mu * np.eye(p), shrinkage


def distancias(x, media, escala, precisao):
    z = (x - media) / escala
    return np.sqrt(np.maximum(np.einsum("ij,jk,ik->i", z, precisao, z), 0.0))


def matriz_confusao(verdade, predito):
    vp = int(np.sum(verdade & predito))
    vn = int(np.sum(~verdade & ~predito))
    fp = int(np.sum(~verdade & predito))
    fn = int(np.sum(verdade & ~predito))
    total = vp + vn + fp + fn
    return {
        "VP": vp, "VN": vn, "FP": fp, "FN": fn,
        "acuracia": (vp + vn) / total if total else math.nan,
        "taxa_fp": fp / (fp + vn) if (fp + vn) else math.nan,
        "recall": vp / (vp + fn) if (vp + fn) else math.nan,
    }


def literal_c(v):
    s = f"{float(v):.9g}"
    if not any(c in s for c in ".en"):
        s += ".0"
    return s + "f"


def escrever_modelo(caminho, media, escala, precisao, limiar, meta):
    vetor = lambda v: ", ".join(literal_c(x) for x in v)
    linhas = ",\n".join(f"        {{{vetor(linha)}}}" for linha in precisao)
    caminho.write_text(
        f"""/* GERADO por tools/classificador/treinar_detector.py em {meta['quando']} — não editar à mão.
 * Treino: {meta['n_treino']} janelas 'saudavel' de {meta['n_arquivos']} arquivo(s);
 * limiar = quantil {meta['quantil']} das distâncias de treino; shrinkage Ledoit-Wolf = {meta['shrinkage']:.3f}.
 */
#pragma once

#include "anomaly_detector.h"

#define MODELO_ANOMALIA_DISPONIVEL 1

static const anomalia_modelo_t MODELO_ANOMALIA = {{
    .media = {{{vetor(media)}}},
    .escala = {{{vetor(escala)}}},
    .precisao = {{
{linhas}
    }},
    .limiar = {literal_c(limiar)},
}};
""",
        encoding="utf-8",
    )


def formatar(v, pct=False):
    if isinstance(v, float) and math.isnan(v):
        return "—"
    return f"{v * 100:.1f}%" if pct else str(v)


def tabela(nome, mc):
    return (
        f"| {nome} | {mc['VP']} | {mc['VN']} | {mc['FP']} | {mc['FN']} | "
        f"{formatar(mc['acuracia'], True)} | {formatar(mc['taxa_fp'], True)} | {formatar(mc['recall'], True)} |"
    )


def main():
    ap = argparse.ArgumentParser(description="Treina o detector de anomalia com os CSVs do cartão SD (RF12/RF15).")
    ap.add_argument("entradas", nargs="+", help="pasta(s) com log_*.csv ou arquivos CSV")
    ap.add_argument("--quantil-limiar", type=float, default=0.995, help="quantil das distâncias de treino usado como limiar")
    ap.add_argument("--fracao-teste", type=float, default=0.3, help="fração final (cronológica) das janelas saudáveis reservada para teste")
    ap.add_argument("--saida-modelo", type=Path, default=MODELO_PADRAO)
    ap.add_argument("--relatorio", type=Path, default=RELATORIO_PADRAO)
    args = ap.parse_args()

    arquivos, janelas = carregar(args.entradas)
    saudaveis = [j for j in janelas if j["rotulo"] == "saudavel"]
    falhas = [j for j in janelas if j["rotulo"] == "falha"]
    print(f"{len(arquivos)} arquivo(s) | {len(janelas)} janelas classificando | "
          f"saudavel={len(saudaveis)} falha={len(falhas)} sem_rotulo={len(janelas) - len(saudaveis) - len(falhas)}")

    corte = int(len(saudaveis) * (1.0 - args.fracao_teste))
    minimo = 2 * len(COLUNAS)
    if corte < minimo:
        sys.exit(f"ERRO: só {corte} janelas 'saudavel' para treino (mínimo {minimo}). "
                 f"Colete mais com o comando 'saudavel' (RF15) ativo.")

    treino, teste_saudavel = saudaveis[:corte], saudaveis[corte:]
    x_treino = np.array([j["x"] for j in treino])
    media = x_treino.mean(axis=0)
    escala = x_treino.std(axis=0)
    escala[escala < 1e-6] = 1.0
    cov, shrinkage = covariancia_ledoit_wolf((x_treino - media) / escala)
    precisao = np.linalg.inv(cov)
    limiar = float(np.quantile(distancias(x_treino, media, escala, precisao), args.quantil_limiar))

    teste = teste_saudavel + falhas
    mc_detector = mc_firmware = None
    if teste:
        x_teste = np.array([j["x"] for j in teste])
        verdade = np.array([j["rotulo"] == "falha" for j in teste])
        mc_detector = matriz_confusao(verdade, distancias(x_teste, media, escala, precisao) > limiar)
        mc_firmware = matriz_confusao(verdade, np.array([j["firmware_alerta"] for j in teste]))

    if not (teste_saudavel and falhas):
        portao = "INCONCLUSIVO — faltam janelas 'falha' ou 'saudavel' de teste"
    else:
        acc, rec = mc_detector["acuracia"], mc_detector["recall"]
        atinge_meta = acc >= META_ACURACIA and rec >= META_RECALL
        if atinge_meta and acc > mc_firmware["acuracia"]:
            portao = "APROVADO — detector atinge a meta da §6 e supera os limiares (pode complementar/substituir)"
        elif atinge_meta:
            portao = "detector atinge a meta da §6, mas não supera os limiares — limiares seguem como solução"
        else:
            portao = (f"REPROVADO — detector abaixo da meta da §6 (acurácia {acc:.0%}, recall {rec:.0%}); "
                      "limiares seguem como solução e o detector fica como investigação (§5.4)")

    quando = dt.datetime.now().strftime("%Y-%m-%d %H:%M")
    escrever_modelo(args.saida_modelo, media, escala, precisao, limiar, {
        "quando": quando, "n_treino": len(treino), "n_arquivos": len(arquivos),
        "quantil": args.quantil_limiar, "shrinkage": shrinkage,
    })

    linhas_rel = [
        "# Relatório do detector de anomalia (Mahalanobis)",
        "",
        f"Gerado em {quando} a partir de {len(arquivos)} arquivo(s) CSV.",
        "",
        f"- Janelas classificando: {len(janelas)} (saudavel={len(saudaveis)}, falha={len(falhas)})",
        f"- Treino: {len(treino)} janelas saudáveis (primeiros {100 * (1 - args.fracao_teste):.0f}%, ordem cronológica)",
        f"- Teste: {len(teste_saudavel)} saudáveis + {len(falhas)} falha",
        f"- Limiar: {limiar:.4f} (quantil {args.quantil_limiar}); shrinkage Ledoit-Wolf: {shrinkage:.3f}",
        "",
    ]
    if mc_detector:
        horas = len(teste_saudavel) / 3600.0
        linhas_rel += [
            "| Método | VP | VN | FP | FN | Acurácia | Taxa FP | Recall |",
            "|---|---|---|---|---|---|---|---|",
            tabela("Detector (Mahalanobis)", mc_detector),
            tabela("Limiares do firmware (votação)", mc_firmware),
            "",
        ]
        if horas > 0:
            linhas_rel.append(f"- Janelas falsas por hora de operação saudável: detector {mc_detector['FP'] / horas:.1f}, "
                              f"firmware {mc_firmware['FP'] / horas:.1f}")
    linhas_rel += ["", f"**Portão de decisão (§5.4):** {portao}", ""]
    args.relatorio.write_text("\n".join(linhas_rel), encoding="utf-8")

    print(f"limiar={limiar:.4f} shrinkage={shrinkage:.3f}")
    if mc_detector:
        print(tabela("Detector", mc_detector))
        print(tabela("Firmware", mc_firmware))
    print(f"Portão: {portao}")
    print(f"Modelo: {args.saida_modelo}")
    print(f"Relatório: {args.relatorio}")


if __name__ == "__main__":
    main()
