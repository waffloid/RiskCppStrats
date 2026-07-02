#!/usr/bin/env python3
"""Figure for the economy benchmark: per-seed efficiency by solver.

Reads the raw CSVs already produced by run.py; writes
results/economy/figures/efficiency.pdf -- one dot per graph seed, solvers
ordered by mean, both graph sizes as adjacent columns of panels.

Usage: python3 -m experiments.economy_bench.plot
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, read_csv, mean  # noqa: E402
from experiments.plotstyle import plt, PALETTE, GRAY, style_axes  # noqa: E402

SOLVERS = ["greedy", "bootstrap", "mcmc", "qubo", "qubo_adjacency",
           "qubo_distance", "qubo_degree", "qubo_composite",
           "qubo_factory_biased"]
SIZES = [150, 300]


def main():
    outdir = RESULTS_ROOT / "economy"
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)

    data = {}
    for n in SIZES:
        for s in SOLVERS:
            rows = read_csv(outdir / "raw" / f"{s}_n{n}.csv")
            data[(s, n)] = [float(r["efficiency"]) for r in rows]

    order = sorted(SOLVERS, key=lambda s: mean(data[(s, SIZES[0])]))

    fig, axes = plt.subplots(1, 2, figsize=(7.6, 3.0), sharey=True, sharex=True)
    for ax, n in zip(axes, SIZES):
        for yi, s in enumerate(order):
            vals = data[(s, n)]
            ax.scatter(vals, [yi] * len(vals), s=16, color=PALETTE[0],
                       alpha=0.45, linewidths=0, zorder=2)
            m = mean(vals)
            ax.plot([m, m], [yi - 0.28, yi + 0.28], color=PALETTE[5],
                    linewidth=2.0, zorder=3)
        ax.set_title(f"{n} nodes")
        ax.set_xlabel("efficiency (production / relaxed bound)")
        style_axes(ax)
        ax.grid(axis="y", visible=False)
    axes[0].set_yticks(range(len(order)),
                       [s.replace("_", " ") for s in order])
    fig.tight_layout()
    fig.savefig(figdir / "efficiency.pdf")
    print(f"wrote {figdir / 'efficiency.pdf'}")


if __name__ == "__main__":
    main()
