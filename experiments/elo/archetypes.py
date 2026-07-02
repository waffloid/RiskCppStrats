#!/usr/bin/env python3
"""Archetype triangle: does strategic posture alone induce an RPS cycle?

Three models share one chassis (v12's QUBO economy + OT transport) and differ
only in posture (src/ai/models/archetypes.cpp):
  arch_rock     -- aggressive (war weight 5, front-line pull 6, no garrison)
  arch_paper    -- defensive, distrustful (garrison 150, war weight 0.25)
  arch_scissors -- defensive, trusting (no war agent, no garrison)

Hypothesis (Joel): rock > scissors (steamroll), scissors > paper (leaner
allocation), paper > rock (defender's advantage). Symmetric duels,
GAMES_PER_PAIR per pair, sides alternated.

Usage: python3 -m experiments.elo.archetypes [--games-per-pair=12]
"""

import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import results_dir, write_manifest, write_csv, write_tex_table  # noqa: E402
from experiments.elo.run import run_game  # noqa: E402

ARCHETYPES = ["arch_rock", "arch_paper", "arch_scissors"]
PREDICTION = {("arch_rock", "arch_scissors"): ">",
              ("arch_scissors", "arch_paper"): ">",
              ("arch_paper", "arch_rock"): ">"}
BASE_SEED = 9000
DT = 16


def main():
    games_per_pair, workers = 12, 10
    for arg in sys.argv[1:]:
        if arg.startswith("--games-per-pair="):
            games_per_pair = int(arg.split("=")[1])

    outdir = results_dir("archetypes")
    write_manifest(outdir, {"models": ARCHETYPES, "games_per_pair": games_per_pair,
                            "dt": DT, "base_seed": BASE_SEED,
                            "protocol": "symmetric 1v1 duel",
                            "hypothesis": "rock>scissors, scissors>paper, paper>rock"})

    pairs = [(a, b) for i, a in enumerate(ARCHETYPES) for b in ARCHETYPES[i + 1:]]
    jobs, seed = [], BASE_SEED
    for a, b in pairs:
        for k in range(games_per_pair):
            solo, duo = (a, b) if k % 2 == 0 else (b, a)
            jobs.append((solo, duo, seed))
            seed += 1

    rows, score, count = [], defaultdict(lambda: defaultdict(float)), defaultdict(lambda: defaultdict(int))
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futures = {ex.submit(run_game, s, d, sd, DT, "duel"): (s, d, sd) for s, d, sd in jobs}
        for f in as_completed(futures):
            solo, duo, sd = futures[f]
            sc, ticks = f.result()
            if sc is None:
                continue
            rows.append([solo, duo, sd, DT, sc, ticks])
            score[solo][duo] += sc
            score[duo][solo] += 1.0 - sc
            count[solo][duo] += 1
            count[duo][solo] += 1

    rows.sort(key=lambda r: r[2])
    write_csv(outdir / "raw" / "games.csv",
              ["solo", "duo", "seed", "dt", "score_for_solo", "final_tick"], rows)

    tex_rows, out_rows = [], []
    print("\nARCHETYPE TRIANGLE (duel)")
    for (a, b), pred in PREDICTION.items():
        rate = score[a][b] / count[a][b]
        outcome = ">" if rate > 0.5 else ("<" if rate < 0.5 else "=")
        ok = "confirmed" if outcome == pred else "REFUTED"
        short_a, short_b = a.replace("arch_", ""), b.replace("arch_", "")
        print(f"  {short_a:9s} vs {short_b:9s}: {rate:.2f}  predicted {short_a}{pred}{short_b}  -> {ok}")
        tex_rows.append([f"{short_a} vs.\\ {short_b}", f"{rate:.2f}",
                         f"{short_a} $>$ {short_b}", ok])
        out_rows.append([a, b, f"{rate:.3f}", count[a][b], pred, outcome, ok])

    write_csv(outdir / "summary.csv",
              ["model_a", "model_b", "score_rate_a", "games", "predicted", "observed", "verdict"],
              out_rows)
    write_tex_table(outdir / "tables" / "archetypes.tex",
                    ["Matchup", "Score rate", "Predicted", "Verdict"],
                    tex_rows, colspec="lrll")


if __name__ == "__main__":
    main()
