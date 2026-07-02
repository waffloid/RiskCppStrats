#!/usr/bin/env python3
"""The lineage as an ablation study.

Each lineage step adds (approximately) one feature, so the child-vs-parent
head-to-head measures that feature's marginal value with everything else
held fixed. Reads the existing duel and FFA game logs -- no new games --
and emits results/elo_duel/tables/ablation.tex.

Usage: python3 -m experiments.elo.ablations
"""

import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import RESULTS_ROOT, write_csv, write_tex_table  # noqa: E402

# (child, parent, feature added). Steps marked ~ are approximate parents.
STEPS = [
    ("v1_knapsack", "v0_expansion", "war agent (drops expansion; confounded)"),
    ("v1_knapsack_hybrid", "v1_knapsack", "expansion restored at half weight"),
    ("v4", "v1_knapsack_hybrid", "knapsack $\\to$ priority-rule war"),
    ("v5_qubo", "v4", "layout via annealed MAX-CUT"),
    ("v6", "v5_qubo", "factory-biased layout"),
    ("v7", "v6", "construction discipline"),
    ("v8", "v6", "local softmax (branch)"),
    ("v9", "v6", "OT transport ($\\sim$parent)"),
    ("v10", "v9", "construction discipline"),
    ("v11", "v10", "en-route request netting"),
    ("v12", "v11", "border reserve"),
]


def pair_rates(name):
    score = defaultdict(lambda: defaultdict(float))
    count = defaultdict(lambda: defaultdict(int))
    with open(RESULTS_ROOT / name / "raw" / "games.csv") as f:
        for g in csv.DictReader(f):
            s = float(g["score_for_solo"])
            score[g["solo"]][g["duo"]] += s
            score[g["duo"]][g["solo"]] += 1.0 - s
            count[g["solo"]][g["duo"]] += 1
            count[g["duo"]][g["solo"]] += 1
    return score, count


def main():
    d_score, d_count = pair_rates("elo_duel")
    f_score, f_count = pair_rates("elo")

    elo = {}
    with open(RESULTS_ROOT / "elo_duel" / "summary.csv") as f:
        for row in csv.DictReader(f):
            elo[row["model"]] = float(row["elo"])

    rows, csv_rows = [], []
    print("ABLATION LADDER (child vs parent)")
    for child, parent, feature in STEPS:
        dr = d_score[child][parent] / d_count[child][parent]
        fr = f_score[child][parent] / f_count[child][parent]
        de = elo[child] - elo[parent]
        arrow = parent.replace("_expansion", "").replace("_knapsack", "k") \
            .replace("_hybrid", "h") + " $\\to$ " + \
            child.replace("_expansion", "").replace("_knapsack", "k") \
            .replace("_hybrid", "h").replace("_qubo", "q")
        rows.append([arrow, feature, f"{dr:.2f}", f"{fr:.2f}", f"{de:+.0f}"])
        csv_rows.append([parent, child, feature, f"{dr:.3f}", f"{fr:.3f}",
                         f"{de:.1f}"])
        print(f"  {parent:20s} -> {child:20s} duel {dr:.2f}  ffa {fr:.2f}  "
              f"dElo {de:+7.0f}  ({feature})")

    write_csv(RESULTS_ROOT / "elo_duel" / "ablation.csv",
              ["parent", "child", "feature", "duel_h2h_child",
               "ffa_h2h_child", "elo_delta"], csv_rows)
    write_tex_table(
        RESULTS_ROOT / "elo_duel" / "tables" / "ablation.tex",
        ["Step", "Feature added", "Duel h2h", "FFA h2h", "$\\Delta$Elo"],
        rows, colspec="llrrr")


if __name__ == "__main__":
    main()
