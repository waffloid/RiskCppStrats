#!/usr/bin/env python3
"""Figure for the combat battery: per-tick anatomy of one corridor battle.

Re-runs the headline matchup (v12 vs v4 on corridor, run 0 of the battery)
with per-tick collection and plots troop counts and cumulative kills.

Usage: python3 -m experiments.combat_bench.plot
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import BUILD_DIR, RESULTS_ROOT, read_csv  # noqa: E402
from experiments.plotstyle import plt, PALETTE, style_axes  # noqa: E402

SOLVER, OPPONENT, BENCH = "v12", "v4", "corridor"


def main():
    outdir = RESULTS_ROOT / "combat"
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)
    per_tick = outdir / "raw" / f"{SOLVER}_{BENCH}_per_tick.csv"

    cmd = [str(BUILD_DIR / "gym_combat"), f"--solver={SOLVER}",
           f"--opponent={OPPONENT}", f"--benchmark={BENCH}", "--runs=1",
           f"--output={outdir / 'raw' / 'per_tick_run.csv'}",
           f"--per-tick={per_tick}"]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True, capture_output=True, text=True)

    rows = read_csv(per_tick)
    xs = [int(r["tick"]) for r in rows]

    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.7))
    for prefix, color, label in [("p0", PALETTE[0], SOLVER),
                                 ("p1", PALETTE[1], OPPONENT)]:
        axes[0].plot(xs, [float(r[f"{prefix}_troops"]) for r in rows],
                     color=color, label=label)
        axes[1].plot(xs, [float(r[f"{prefix}_kills"]) for r in rows],
                     color=color, label=label)
    axes[0].set_ylabel("total troops")
    axes[1].set_ylabel("cumulative kills")
    for ax in axes:
        ax.set_xlabel("tick")
        style_axes(ax)
    axes[0].legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(figdir / "corridor_battle.pdf")
    print(f"wrote {figdir / 'corridor_battle.pdf'}")


if __name__ == "__main__":
    main()
