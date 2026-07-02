#!/usr/bin/env python3
"""Combat benchmark battery: war models on fixed tactical maps.

Runs gym_combat for each candidate model against a fixed baseline opponent
on every benchmark map (building disabled -- pure combat). Scores by wins
and kill/death ratio. Complements the Elo ladder: this isolates tactical
combat from economy and expansion.

Usage: python3 -m experiments.combat_bench.run [--opponent=v4] [--runs=3]
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import (  # noqa: E402
    BUILD_DIR, results_dir, write_manifest, write_csv, write_tex_table, read_csv,
)

MODELS = ["v1_knapsack", "v4", "v9", "v12"]
BENCHMARKS = ["corridor", "bipartite_3_5", "star_hub", "grid_5x5"]


def main():
    opponent, runs = "v4", 3
    for arg in sys.argv[1:]:
        if arg.startswith("--opponent="):
            opponent = arg.split("=")[1]
        elif arg.startswith("--runs="):
            runs = int(arg.split("=")[1])

    outdir = results_dir("combat")
    write_manifest(outdir, {"models": MODELS, "benchmarks": BENCHMARKS,
                            "opponent": opponent, "runs": runs})

    all_rows = []
    for model in MODELS:
        for bench in BENCHMARKS:
            raw = outdir / "raw" / f"{model}_{bench}.csv"
            cmd = [str(BUILD_DIR / "gym_combat"), f"--solver={model}",
                   f"--opponent={opponent}", f"--benchmark={bench}",
                   f"--runs={runs}", f"--output={raw}"]
            print(" ".join(cmd))
            subprocess.run(cmd, check=True, capture_output=True, text=True)
            for r in read_csv(raw):
                all_rows.append({**r, "model": model, "bench": bench})

    summary = []
    for model in MODELS:
        rows = [r for r in all_rows if r["model"] == model]
        wins = sum(1 for r in rows if r["winner"] == "0")
        draws = sum(1 for r in rows if r["winner"] == "-1")
        kills = sum(float(r["deaths_p1"]) for r in rows)
        deaths = sum(float(r["deaths_p0"]) for r in rows)
        per_bench = {}
        for bench in BENCHMARKS:
            br = [r for r in rows if r["bench"] == bench]
            per_bench[bench] = sum(1 for r in br if r["winner"] == "0")
        summary.append({"model": model, "wins": wins, "draws": draws,
                        "games": len(rows), "kd": kills / deaths if deaths else float("inf"),
                        "per_bench": per_bench})
    summary.sort(key=lambda s: -s["wins"])

    write_csv(outdir / "summary.csv",
              ["rank", "model", "wins", "draws", "games", "kd"] + BENCHMARKS,
              [[i + 1, s["model"], s["wins"], s["draws"], s["games"], f"{s['kd']:.2f}"]
               + [s["per_bench"][b] for b in BENCHMARKS]
               for i, s in enumerate(summary)])

    write_tex_table(
        outdir / "tables" / "combat.tex",
        ["Rank", "Model", "Wins", "K/D"] + [b.replace("_", " ") for b in BENCHMARKS],
        [[i + 1, s["model"], f"{s['wins']}/{s['games']}", f"{s['kd']:.2f}"]
         + [f"{s['per_bench'][b]}/{runs}" for b in BENCHMARKS]
         for i, s in enumerate(summary)],
        colspec="llrr" + "r" * len(BENCHMARKS))

    print(f"\nCOMBAT BATTERY (vs {opponent})")
    for i, s in enumerate(summary):
        print(f"  {i+1}. {s['model']:15s} {s['wins']}/{s['games']} wins, {s['draws']} draws")


if __name__ == "__main__":
    main()
