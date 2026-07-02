#!/usr/bin/env python3
"""HodgeRank analysis of the tournament game log (no new games needed).

Treat the mean score rate between each pair as a skew-symmetric edge flow
Y_ij = scorerate(i over j) - 1/2 on the complete comparison graph. Hodge
decomposition (Jiang-Lim-Yao-Ye 2011) splits Y into a gradient flow (the part
explained by a single potential/ranking) plus a residual; on a complete graph
the harmonic part vanishes, so the residual is pure curl (local cycles).

The gradient fraction ||grad||^2 / ||Y||^2 is a single number for how
transitive the tournament is -- equivalently, how lossy ANY one-dimensional
rating (Elo included) must be. The largest-curl triangles name the actual
rock-paper-scissors cycles.

Usage: python3 -m experiments.elo.hodge
"""

import csv
import math
import sys
from collections import defaultdict
from itertools import combinations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, write_csv, write_tex_table  # noqa: E402


def main():
    outdir = RESULTS_ROOT / "elo"
    with open(outdir / "raw" / "games.csv") as f:
        games = list(csv.DictReader(f))

    score = defaultdict(lambda: defaultdict(float))
    count = defaultdict(lambda: defaultdict(int))
    models = sorted({g["solo"] for g in games} | {g["duo"] for g in games})
    for g in games:
        s = float(g["score_for_solo"])
        score[g["solo"]][g["duo"]] += s
        score[g["duo"]][g["solo"]] += 1.0 - s
        count[g["solo"]][g["duo"]] += 1
        count[g["duo"]][g["solo"]] += 1

    # Skew-symmetric pairwise flow on the complete graph.
    Y = {(a, b): score[a][b] / count[a][b] - 0.5
         for a, b in combinations(models, 2) if count[a][b]}

    # Gradient (least-squares potential). On K_n: s_i = row-mean of flows.
    n = len(models)
    pot = {m: sum((Y[(m, o)] if (m, o) in Y else -Y[(o, m)])
                  for o in models if o != m) / n for m in models}
    mean_pot = sum(pot.values()) / n
    pot = {m: v - mean_pot for m, v in pot.items()}

    grad_sq = flow_sq = 0.0
    resid = {}
    for (a, b), y in Y.items():
        g = pot[a] - pot[b]
        resid[(a, b)] = y - g
        flow_sq += y * y
        grad_sq += g * g
    resid_sq = sum(r * r for r in resid.values())
    transitivity = grad_sq / flow_sq

    def r(a, b):
        return resid[(a, b)] if (a, b) in resid else -resid[(b, a)]

    # Curl per triangle: sum of residual flow around the 3-cycle.
    triangles = []
    for a, b, c in combinations(models, 3):
        curl = r(a, b) + r(b, c) + r(c, a)
        triangles.append((abs(curl), curl, (a, b, c)))
    triangles.sort(reverse=True)

    ranked = sorted(models, key=lambda m: -pot[m])
    write_csv(outdir / "hodge_summary.csv",
              ["rank", "model", "hodge_potential"],
              [[i + 1, m, f"{pot[m]:.4f}"] for i, m in enumerate(ranked)])

    stats = {
        "flow_norm_sq": flow_sq, "grad_norm_sq": grad_sq,
        "curl_norm_sq": resid_sq, "transitivity": transitivity,
    }
    write_csv(outdir / "hodge_stats.csv", list(stats), [list(stats.values())])

    # Elo (from summary.csv) for side-by-side.
    elo = {}
    with open(outdir / "summary.csv") as f:
        for row in csv.DictReader(f):
            elo[row["model"]] = float(row["elo"])
    elo_ranked = sorted(models, key=lambda m: -elo[m])

    write_tex_table(
        outdir / "tables" / "hodge.tex",
        ["Rank", "Model", "Hodge potential $s_i$", "BT/Elo rank"],
        [[i + 1, m, f"{pot[m]:+.3f}", elo_ranked.index(m) + 1]
         for i, m in enumerate(ranked)])

    write_tex_table(
        outdir / "tables" / "curl_triangles.tex",
        ["Cycle", "Curl"],
        [[" $\\to$ ".join(t) + " $\\to$ " + t[0], f"{c:+.2f}"]
         for _, c, t in triangles[:5]],
        colspec="lr")

    # Spearman rank correlation between Hodge potential and Elo.
    rank_h = {m: i for i, m in enumerate(ranked)}
    rank_e = {m: i for i, m in enumerate(elo_ranked)}
    d_sq = sum((rank_h[m] - rank_e[m]) ** 2 for m in models)
    spearman = 1 - 6 * d_sq / (n * (n * n - 1))

    with open(outdir / "hodge_macros.tex", "w") as f:
        f.write(f"\\newcommand{{\\hodgetransitivity}}{{{100*transitivity:.1f}\\%}}\n")
        f.write(f"\\newcommand{{\\hodgecurl}}{{{100*resid_sq/flow_sq:.1f}\\%}}\n")
        f.write(f"\\newcommand{{\\hodgespearman}}{{{spearman:.2f}}}\n")

    print(f"transitivity (grad fraction): {transitivity:.3f}")
    print(f"curl fraction:                {resid_sq / flow_sq:.3f}")
    print(f"spearman(hodge, elo):         {spearman:.3f}")
    print("top curl triangles:")
    for _, c, t in triangles[:5]:
        print(f"  {' -> '.join(t)} -> {t[0]}: {c:+.2f}")

    # Scatter: Hodge potential vs Elo.
    from experiments.plotstyle import plt, PALETTE, GRAY, style_axes
    fig, ax = plt.subplots(figsize=(4.6, 3.4))
    xs = [pot[m] for m in models]
    ys = [elo[m] for m in models]
    ax.scatter(xs, ys, s=42, color=PALETTE[0], zorder=3)
    for m in models:
        ax.annotate(m.replace("_expansion", "").replace("_knapsack", "k")
                     .replace("_hybrid", "h").replace("_qubo", "q")
                     .replace("_composite", "c"),
                    (pot[m], elo[m]), textcoords="offset points",
                    xytext=(6, -2), fontsize=7.5, color="#333333")
    ax.set_xlabel("Hodge potential $s_i$ (score-rate units)")
    ax.set_ylabel("Bradley--Terry rating (Elo scale)")
    style_axes(ax)
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)
    fig.savefig(figdir / "hodge_vs_elo.pdf")
    print(f"wrote {figdir / 'hodge_vs_elo.pdf'}")


if __name__ == "__main__":
    main()
