#!/usr/bin/env python3
"""Cross-protocol comparison: duel (1v1) ladder vs FFA (solo-vs-duo) ladder.

Reads both summary.csv files, emits a combined ladder table (duel as the
headline ordering, FFA rank alongside) and a Spearman macro for the writeup.

Usage: python3 -m experiments.elo.compare
"""

import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, write_tex_table  # noqa: E402


def load(name):
    rows = {}
    with open(RESULTS_ROOT / name / "summary.csv") as f:
        for r in csv.DictReader(f):
            rows[r["model"]] = r
    return rows


def main():
    duel, ffa = load("elo_duel"), load("elo")
    models = sorted(duel, key=lambda m: int(duel[m]["rank"]))

    write_tex_table(
        RESULTS_ROOT / "elo_duel" / "tables" / "ladder_combined.tex",
        ["Rank", "Model", "Elo (duel)", "Score rate", "FFA rank", "FFA Elo"],
        [[duel[m]["rank"], m, f"{float(duel[m]['elo']):.0f}",
          f"{float(duel[m]['winrate']):.2f}", ffa[m]["rank"],
          f"{float(ffa[m]['elo']):.0f}"] for m in models],
        colspec="llrrrr")

    n = len(models)
    d_sq = sum((int(duel[m]["rank"]) - int(ffa[m]["rank"])) ** 2 for m in models)
    spearman = 1 - 6 * d_sq / (n * (n * n - 1))
    with open(RESULTS_ROOT / "elo_duel" / "protocol_macros.tex", "w") as f:
        f.write(f"\\newcommand{{\\ffaduelspearman}}{{{spearman:.2f}}}\n")

    print(f"spearman(duel, ffa) = {spearman:.3f}")
    for m in models:
        print(f"  {duel[m]['rank']:>2s}. {m:22s} duel {float(duel[m]['elo']):7.1f}  "
              f"(ffa rank {ffa[m]['rank']})")


if __name__ == "__main__":
    main()
