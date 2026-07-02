#!/usr/bin/env python3
"""Figures for the transport benchmark: loss-vs-time curves per preset.

Reads the per-tick CSVs already produced by run.py; writes
results/transport/figures/loss_curves.pdf (small multiples, log-scale loss).

Usage: python3 -m experiments.transport_bench.plot
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, read_csv  # noqa: E402
from experiments.plotstyle import plt, PALETTE, style_axes  # noqa: E402

PRESETS = ["star_center", "path_end", "bipartite_split",
           "poisson_edge_ring", "poisson_3_clusters", "poisson_capital_rush"]
SOLVERS = [("greedy", PALETTE[0]), ("ot", PALETTE[1])]


def main():
    outdir = RESULTS_ROOT / "transport"
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)

    fig, axes = plt.subplots(2, 3, figsize=(8.2, 4.6), sharex=True)
    for ax, preset in zip(axes.flat, PRESETS):
        for solver, color in SOLVERS:
            rows = read_csv(outdir / "raw" / f"{preset}_{solver}.csv")
            xs = [int(r["tick"]) for r in rows]
            # clip zeros for the log scale; loss < 1 means converged
            ys = [max(float(r["loss"]), 0.5) for r in rows]
            ax.plot(xs, ys, color=color, label=solver)
        ax.set_yscale("log")
        ax.set_title(preset.replace("_", " "))
        style_axes(ax)
    for ax in axes[1]:
        ax.set_xlabel("tick")
    for ax in axes[:, 0]:
        ax.set_ylabel("$L^1$ loss")
    axes[0, 0].legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(figdir / "loss_curves.pdf")
    print(f"wrote {figdir / 'loss_curves.pdf'}")


if __name__ == "__main__":
    main()
