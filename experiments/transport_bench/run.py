#!/usr/bin/env python3
"""Transport solver benchmark: convergence speed to a target distribution.

Runs gym_transport for every (preset, solver) pair; scores by ticks to
converge (first tick with L1 loss < 1), final loss, and loss integral
(sum of per-tick loss -- lower means troops spent less time out of place).

Usage: python3 -m experiments.transport_bench.run [--ticks=800]
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import (  # noqa: E402
    BUILD_DIR, results_dir, write_manifest, write_csv, write_tex_table, read_csv,
)

PRESETS = ["star_center", "path_end", "bipartite_split",
           "poisson_edge_ring", "poisson_3_clusters", "poisson_capital_rush"]
SOLVERS = ["greedy", "ot"]
LOSS = "l1"


def main():
    ticks = 800
    for arg in sys.argv[1:]:
        if arg.startswith("--ticks="):
            ticks = int(arg.split("=")[1])

    outdir = results_dir("transport")
    write_manifest(outdir, {"presets": PRESETS, "solvers": SOLVERS,
                            "loss": LOSS, "ticks": ticks})

    summary = []
    for preset in PRESETS:
        for solver in SOLVERS:
            raw = outdir / "raw" / f"{preset}_{solver}.csv"
            cmd = [str(BUILD_DIR / "gym_transport"), f"--preset={preset}",
                   f"--solver={solver}", f"--loss={LOSS}",
                   f"--ticks={ticks}", f"--output={raw}"]
            print(" ".join(cmd))
            subprocess.run(cmd, check=True, capture_output=True, text=True)

            per_tick = read_csv(raw)
            losses = [float(r["loss"]) for r in per_tick]
            converge = next((int(r["tick"]) for r in per_tick if float(r["loss"]) < 1.0), None)
            summary.append({
                "preset": preset, "solver": solver,
                "initial_loss": losses[0] if losses else float("nan"),
                "final_loss": losses[-1] if losses else float("nan"),
                "ticks_to_converge": converge,
                "loss_integral": sum(losses),
            })

    write_csv(outdir / "summary.csv",
              ["preset", "solver", "initial_loss", "final_loss",
               "ticks_to_converge", "loss_integral"],
              [[s["preset"], s["solver"], f"{s['initial_loss']:.0f}",
                f"{s['final_loss']:.1f}",
                s["ticks_to_converge"] if s["ticks_to_converge"] is not None else "never",
                f"{s['loss_integral']:.0f}"] for s in summary])

    rows = []
    for preset in PRESETS:
        entries = {s["solver"]: s for s in summary if s["preset"] == preset}
        row = [preset]
        for solver in SOLVERS:
            e = entries[solver]
            conv = str(e["ticks_to_converge"]) if e["ticks_to_converge"] is not None else "--"
            row += [conv, f"{e['loss_integral']:.0f}"]
        rows.append(row)
    write_tex_table(
        outdir / "tables" / "transport.tex",
        ["Preset", "Greedy: conv.", "Greedy: $\\int$loss", "OT: conv.", "OT: $\\int$loss"],
        rows, colspec="lrrrr")

    print("\nTRANSPORT RESULTS")
    for s in summary:
        print(f"  {s['preset']:22s} {s['solver']:8s} converge={s['ticks_to_converge']} "
              f"integral={s['loss_integral']:.0f} final={s['final_loss']:.1f}")


if __name__ == "__main__":
    main()
