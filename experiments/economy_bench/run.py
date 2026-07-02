#!/usr/bin/env python3
"""Economy solver benchmark: production efficiency vs theoretical max.

Runs gym_economy for every registered solver over a common set of seeds and
graph sizes; ranks solvers by mean efficiency (production / theoretical max).

Usage: python3 -m experiments.economy_bench.run [--runs=10] [--nodes=150,300]
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import (  # noqa: E402
    BUILD_DIR, results_dir, write_manifest, write_csv, write_tex_table,
    read_csv, mean, stdev,
)

SOLVERS = [
    "greedy", "bootstrap", "mcmc",
    "qubo", "qubo_adjacency", "qubo_distance", "qubo_degree",
    "qubo_composite", "qubo_factory_biased",
]
SEED = 42


def main():
    runs, node_sizes = 10, [150, 300]
    for arg in sys.argv[1:]:
        if arg.startswith("--runs="):
            runs = int(arg.split("=")[1])
        elif arg.startswith("--nodes="):
            node_sizes = [int(x) for x in arg.split("=")[1].split(",")]

    outdir = results_dir("economy")
    write_manifest(outdir, {"solvers": SOLVERS, "runs": runs, "seed": SEED,
                            "node_sizes": node_sizes})

    all_rows = []
    for nodes in node_sizes:
        for solver in SOLVERS:
            raw = outdir / "raw" / f"{solver}_n{nodes}.csv"
            cmd = [str(BUILD_DIR / "gym_economy"), f"--solver={solver}",
                   f"--seed={SEED}", f"--runs={runs}", f"--nodes={nodes}",
                   f"--output={raw}"]
            print(" ".join(cmd))
            subprocess.run(cmd, check=True, capture_output=True, text=True)
            for r in read_csv(raw):
                # the gym writes a numeric solver id; keep the name from the loop
                all_rows.append({**r, "solver_name": solver, "nodes_setting": nodes})

    # aggregate: mean/std efficiency per (solver, size)
    summary = []
    for nodes in node_sizes:
        for solver in SOLVERS:
            effs = [float(r["efficiency"]) for r in all_rows
                    if r["solver_name"] == solver and r["nodes_setting"] == nodes]
            prods = [float(r["production_rate"]) for r in all_rows
                     if r["solver_name"] == solver and r["nodes_setting"] == nodes]
            summary.append({"nodes": nodes, "solver": solver,
                            "eff_mean": mean(effs), "eff_std": stdev(effs),
                            "prod_mean": mean(prods), "n": len(effs)})

    summary.sort(key=lambda s: (s["nodes"], -s["eff_mean"]))
    write_csv(outdir / "summary.csv",
              ["nodes", "rank", "solver", "eff_mean", "eff_std", "prod_mean", "n"],
              [[s["nodes"], i + 1, s["solver"], f"{s['eff_mean']:.4f}",
                f"{s['eff_std']:.4f}", f"{s['prod_mean']:.1f}", s["n"]]
               for nodes in node_sizes
               for i, s in enumerate([x for x in summary if x["nodes"] == nodes])])

    for nodes in node_sizes:
        rows = [x for x in summary if x["nodes"] == nodes]
        write_tex_table(
            outdir / "tables" / f"economy_n{nodes}.tex",
            ["Rank", "Solver", "Efficiency", "Production", "Runs"],
            [[i + 1, s["solver"],
              f"{s['eff_mean']:.3f} $\\pm$ {s['eff_std']:.3f}",
              f"{s['prod_mean']:.0f}", s["n"]]
             for i, s in enumerate(rows)])

    print("\nECONOMY RANKING")
    for nodes in node_sizes:
        print(f"  n={nodes}:")
        for i, s in enumerate([x for x in summary if x["nodes"] == nodes]):
            print(f"    {i+1}. {s['solver']:22s} eff {s['eff_mean']:.3f} +/- {s['eff_std']:.3f}")


if __name__ == "__main__":
    main()
