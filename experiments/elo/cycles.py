#!/usr/bin/env python3
"""Where does the intransitivity live? Spectral analysis of the curl.

The residual (curl) part of the pairwise flow is a real antisymmetric matrix,
which decomposes into planar rotations: eigenvalues come in pairs +/- i*lambda
and each eigenplane is an independent cycle. Projecting the models onto the
leading eigenplane (the 'gamescape' construction of Balduzzi et al. 2019;
cf. Czarnecki et al. 2020) shows WHO participates in the dominant cycle:
distance from the origin = strength of participation, and going around the
circle in angle order reads out the beats-list of the cycle.

Reads results/<dir>/raw/games.csv; writes the cyclic-plane figure, a phase
table, and macros with the rank-2 concentration of the curl.

Usage: python3 -m experiments.elo.cycles [--dir=elo_duel]
"""

import csv
import math
import sys
from collections import defaultdict
from itertools import combinations
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, write_csv, write_tex_table  # noqa: E402


def main():
    name = "elo_duel"
    for arg in sys.argv[1:]:
        if arg.startswith("--dir="):
            name = arg.split("=")[1]
    outdir = RESULTS_ROOT / name

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

    n = len(models)
    idx = {m: i for i, m in enumerate(models)}
    Y = np.zeros((n, n))
    for a, b in combinations(models, 2):
        if count[a][b]:
            y = score[a][b] / count[a][b] - 0.5
            Y[idx[a], idx[b]], Y[idx[b], idx[a]] = y, -y

    # Remove the gradient (row-mean potential on K_n), leaving pure curl.
    s_pot = Y.mean(axis=1)
    G = s_pot[:, None] - s_pot[None, :]
    R = Y - G

    # Antisymmetric eigendecomposition: eigenvalues +/- i*lambda_k.
    ev, V = np.linalg.eig(R)
    order = np.argsort(-np.abs(ev.imag))
    lam1 = abs(ev.imag[order[0]])
    v = V[:, order[0]]  # complex eigenvector of the leading plane
    x, y = np.sqrt(2 * lam1) * v.real, np.sqrt(2 * lam1) * v.imag

    # Orient the plane so that increasing angle = direction of "beats".
    total_sq = (R ** 2).sum()
    plane_sq = 2 * lam1 ** 2 * 2  # two conjugate eigenvalues, each lambda^2, x2 sym
    # (||R||_F^2 = 2 * sum_k lambda_k^2 over planes; leading plane share:)
    lams = np.sort(np.abs(ev.imag))[::-1][::2]  # one lambda per plane
    plane_share = lams[0] ** 2 / (lams ** 2).sum() if lams.sum() > 0 else 0.0

    amp = np.hypot(x, y)
    phase = np.degrees(np.arctan2(y, x)) % 360
    ranked = sorted(range(n), key=lambda i: -amp[i])

    write_csv(outdir / "cycle_plane.csv",
              ["model", "amplitude", "phase_deg"],
              [[models[i], f"{amp[i]:.3f}", f"{phase[i]:.1f}"] for i in ranked])
    write_tex_table(outdir / "tables" / "cycle_plane.tex",
                    ["Model", "Amplitude", "Phase"],
                    [[models[i], f"{amp[i]:.2f}", f"{phase[i]:.0f}$^\\circ$"]
                     for i in ranked if amp[i] > 0.05],
                    colspec="lrr")
    with open(outdir / "cycle_macros.tex", "w") as f:
        sfx = "duel" if name == "elo_duel" else ""
        f.write(f"\\newcommand{{\\cycleplaneshare{sfx}}}{{{100*plane_share:.0f}\\%}}\n")

    print(f"curl planes (lambda): {[f'{l:.2f}' for l in lams if l > 1e-9]}")
    print(f"leading plane share of curl energy: {plane_share:.2f}")
    for i in ranked:
        print(f"  {models[i]:22s} amp {amp[i]:.3f}  phase {phase[i]:6.1f} deg")

    # Figure: models on the leading cyclic plane.
    from experiments.plotstyle import plt, PALETTE, GRAY, style_axes
    fig, ax = plt.subplots(figsize=(4.8, 4.4))
    ax.axhline(0, color="#e6e6e2", lw=0.8, zorder=0)
    ax.axvline(0, color="#e6e6e2", lw=0.8, zorder=0)
    ax.scatter(x, y, s=42, color=PALETTE[0], zorder=3)
    for i in range(n):
        short = (models[i].replace("_expansion", "").replace("_knapsack", "k")
                 .replace("_hybrid", "h").replace("_qubo", "q").replace("_composite", "c"))
        ax.annotate(short, (x[i], y[i]), textcoords="offset points",
                    xytext=(6, 3), fontsize=7.5, color="#333333")
    # rotation direction hint
    theta = np.linspace(0.25 * np.pi, 0.75 * np.pi, 40)
    r_hint = 0.9 * amp.max()
    ax.plot(r_hint * np.cos(theta), r_hint * np.sin(theta), color=GRAY, lw=1.0)
    ax.annotate("", xy=(r_hint * np.cos(theta[0]), r_hint * np.sin(theta[0])),
                xytext=(r_hint * np.cos(theta[3]), r_hint * np.sin(theta[3])),
                arrowprops=dict(arrowstyle="->", color=GRAY))
    ax.set_xlabel("cyclic plane, axis 1")
    ax.set_ylabel("cyclic plane, axis 2")
    ax.set_aspect("equal")
    style_axes(ax)
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)
    fig.savefig(figdir / "cyclic_plane.pdf")
    print(f"wrote {figdir / 'cyclic_plane.pdf'}")


if __name__ == "__main__":
    main()
