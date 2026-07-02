#!/usr/bin/env python3
"""Ordering solver benchmark: build-order quality on a fixed economy plan.

Runs gym_ordering for every ordering solver over common seeds; ranks by
accumulated production (the integral of production over the build-out --
higher means earlier payoff) and total ticks to complete the plan.

Usage: python3 -m experiments.ordering_bench.run [--runs=10] [--economy=mcmc]
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import (  # noqa: E402
    BUILD_DIR, results_dir, write_manifest, write_csv, write_tex_table,
    read_csv, mean, stdev,
)

ORDERINGS = ["sequential", "cheapest_first", "nearest_first", "production_gradient"]
SEED = 42


def main():
    runs, economy = 10, "mcmc"
    for arg in sys.argv[1:]:
        if arg.startswith("--runs="):
            runs = int(arg.split("=")[1])
        elif arg.startswith("--economy="):
            economy = arg.split("=")[1]

    outdir = results_dir("ordering")
    write_manifest(outdir, {"orderings": ORDERINGS, "economy": economy,
                            "runs": runs, "seed": SEED})

    all_rows = []
    for ordering in ORDERINGS:
        raw = outdir / "raw" / f"{ordering}.csv"
        cmd = [str(BUILD_DIR / "gym_ordering"), f"--economy={economy}",
               f"--ordering={ordering}", f"--seed={SEED}", f"--runs={runs}",
               f"--output={raw}"]
        print(" ".join(cmd))
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        for r in read_csv(raw):
            all_rows.append({**r, "ordering": ordering})

    summary = []
    for ordering in ORDERINGS:
        rows = [r for r in all_rows if r["ordering"] == ordering]
        acc = [float(r["accumulated_production"]) for r in rows]
        ticks = [float(r["total_ticks"]) for r in rows]
        summary.append({"ordering": ordering,
                        "acc_mean": mean(acc), "acc_std": stdev(acc),
                        "ticks_mean": mean(ticks), "n": len(rows)})
    summary.sort(key=lambda s: -s["acc_mean"])

    write_csv(outdir / "summary.csv",
              ["rank", "ordering", "acc_production_mean", "acc_production_std",
               "total_ticks_mean", "n"],
              [[i + 1, s["ordering"], f"{s['acc_mean']:.0f}", f"{s['acc_std']:.0f}",
                f"{s['ticks_mean']:.0f}", s["n"]] for i, s in enumerate(summary)])

    write_tex_table(
        outdir / "tables" / "ordering.tex",
        ["Rank", "Ordering", "Accum. production", "Ticks to complete", "Runs"],
        [[i + 1, s["ordering"],
          f"{s['acc_mean']:.0f} $\\pm$ {s['acc_std']:.0f}",
          f"{s['ticks_mean']:.0f}", s["n"]] for i, s in enumerate(summary)])

    print("\nORDERING RANKING (economy=%s)" % economy)
    for i, s in enumerate(summary):
        print(f"  {i+1}. {s['ordering']:22s} accum {s['acc_mean']:.0f} +/- {s['acc_std']:.0f} "
              f"ticks {s['ticks_mean']:.0f}")


if __name__ == "__main__":
    main()
