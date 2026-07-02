#!/usr/bin/env python3
"""Bot ladder: deterministic round-robin, Bradley-Terry ratings on Elo scale.

Protocol (following the original elo_tournament.py): each game is 3-player,
one model "solo" vs two copies of the opponent; the solo model scores 1.0 if
it ends with strictly more nodes than the best opponent copy, 0.5 on a tie.
Each unordered pair plays GAMES_PER_PAIR games, half with each model solo,
with deterministic seeds. Ratings are fit by Bradley-Terry maximum likelihood
(minorization-maximization) on the aggregate score matrix, then mapped to the
Elo scale (1500 + 400*log10 p). Unlike sequential K-factor Elo, the result is
independent of game order and exactly reproducible.

Usage: python3 -m experiments.elo.run [--games-per-pair=4] [--dt=16] [--workers=10]
"""

import math
import re
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import (  # noqa: E402
    BUILD_DIR, results_dir, write_manifest, write_csv, write_tex_table,
)

# Full registry minus v2_knapsack and v3, which are code-identical to v4.
MODELS = [
    "v0_expansion", "v1_knapsack", "v1_knapsack_hybrid", "v4",
    "v5_qubo", "v5_qubo_composite", "v6", "v7", "v8",
    "v9", "v10", "v11", "v12",
]
MAX_TICKS = 5000
BASE_SEED = 1000
TIMEOUT_S = 300


def run_game(solo, duo, seed, dt):
    """Return (score_for_solo, ticks) or (None, None) on failure."""
    pos = seed % 3
    slots = [duo, duo, duo]
    slots[pos] = solo
    cmd = [str(BUILD_DIR / "crisky_headless"), str(seed), str(MAX_TICKS)] + slots + [f"--dt={dt}"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT_S).stdout
    except subprocess.TimeoutExpired:
        return None, None
    for line in reversed(out.strip().split("\n")):
        if line.startswith("tick"):
            ticks = int(re.match(r"tick\s+(\d+)", line).group(1))
            nodes = {int(m.group(1)): int(m.group(2))
                     for m in re.finditer(r"P(\d): \d+ troops, (\d+) nodes", line)}
            solo_n = nodes.get(pos, 0)
            duo_best = max((nodes.get(i, 0) for i in range(3) if i != pos), default=0)
            score = 1.0 if solo_n > duo_best else (0.0 if solo_n < duo_best else 0.5)
            return score, ticks
    return None, None


def bradley_terry(models, score, count, iters=2000):
    """MM fit of BT strengths from fractional scores. score[a][b] = total score of a over b."""
    p = {m: 1.0 for m in models}
    for _ in range(iters):
        new_p = {}
        for i in models:
            w_i = sum(score[i][j] for j in models if j != i)
            denom = sum(count[i][j] / (p[i] + p[j]) for j in models if j != i and count[i][j] > 0)
            new_p[i] = w_i / denom if denom > 0 else p[i]
        # normalize to geometric mean 1
        log_mean = sum(math.log(max(v, 1e-12)) for v in new_p.values()) / len(new_p)
        p = {m: v / math.exp(log_mean) for m, v in new_p.items()}
    return p


def main():
    games_per_pair, dt, workers = 4, 16, 10
    for arg in sys.argv[1:]:
        if arg.startswith("--games-per-pair="):
            games_per_pair = int(arg.split("=")[1])
        elif arg.startswith("--dt="):
            dt = int(arg.split("=")[1])
        elif arg.startswith("--workers="):
            workers = int(arg.split("=")[1])

    outdir = results_dir("elo")
    write_manifest(outdir, {
        "models": MODELS, "games_per_pair": games_per_pair, "dt": dt,
        "max_ticks": MAX_TICKS, "base_seed": BASE_SEED,
        "protocol": "3-player solo-vs-duo, winner by final node count",
    })

    pairs = [(a, b) for i, a in enumerate(MODELS) for b in MODELS[i + 1:]]
    jobs = []  # (solo, duo, seed)
    seed = BASE_SEED
    for a, b in pairs:
        for k in range(games_per_pair):
            solo, duo = (a, b) if k % 2 == 0 else (b, a)
            jobs.append((solo, duo, seed))
            seed += 1

    print(f"{len(MODELS)} models, {len(pairs)} pairs, {len(jobs)} games, dt={dt}, {workers} workers")

    rows = []
    score = defaultdict(lambda: defaultdict(float))
    count = defaultdict(lambda: defaultdict(int))
    done = 0
    with ProcessPoolExecutor(max_workers=workers) as ex:
        futures = {ex.submit(run_game, s, d, sd, dt): (s, d, sd) for s, d, sd in jobs}
        for f in as_completed(futures):
            solo, duo, sd = futures[f]
            sc, ticks = f.result()
            done += 1
            if sc is None:
                print(f"  [{done}/{len(jobs)}] {solo} vs 2x{duo} seed={sd}: FAILED")
                continue
            rows.append([solo, duo, sd, dt, sc, ticks])
            score[solo][duo] += sc
            score[duo][solo] += 1.0 - sc
            count[solo][duo] += 1
            count[duo][solo] += 1
            if done % 25 == 0:
                print(f"  [{done}/{len(jobs)}] games complete")

    rows.sort(key=lambda r: r[2])
    write_csv(outdir / "raw" / "games.csv",
              ["solo", "duo", "seed", "dt", "score_for_solo", "final_tick"], rows)

    p = bradley_terry(MODELS, score, count)
    elo = {m: 1500.0 + 400.0 * math.log10(max(p[m], 1e-12)) for m in MODELS}
    ranked = sorted(MODELS, key=lambda m: -elo[m])

    games_played = {m: sum(count[m][o] for o in MODELS if o != m) for m in MODELS}
    winrate = {m: (sum(score[m][o] for o in MODELS if o != m) / games_played[m])
               if games_played[m] else float("nan") for m in MODELS}

    write_csv(outdir / "summary.csv", ["rank", "model", "elo", "winrate", "games"],
              [[i + 1, m, f"{elo[m]:.1f}", f"{winrate[m]:.3f}", games_played[m]]
               for i, m in enumerate(ranked)])

    write_tex_table(
        outdir / "tables" / "elo_table.tex",
        ["Rank", "Model", "Elo", "Score rate", "Games"],
        [[i + 1, m, f"{elo[m]:.0f}", f"{winrate[m]:.2f}", games_played[m]]
         for i, m in enumerate(ranked)])

    # pairwise score-rate matrix (row model vs column model)
    short = {m: m.replace("_expansion", "").replace("_knapsack", "k").replace("_hybrid", "h")
                  .replace("_qubo", "q").replace("_composite", "c") for m in MODELS}
    matrix_rows = []
    for a in ranked:
        row = [short[a]]
        for b in ranked:
            if a == b:
                row.append("--")
            elif count[a][b]:
                row.append(f"{score[a][b] / count[a][b]:.2f}")
            else:
                row.append("")
        matrix_rows.append(row)
    write_tex_table(outdir / "tables" / "win_matrix.tex",
                    [""] + [short[b] for b in ranked], matrix_rows,
                    colspec="l" + "c" * len(ranked))

    print("\nFINAL LADDER (Bradley-Terry, Elo scale)")
    for i, m in enumerate(ranked):
        print(f"  {i+1:2d}. {m:22s} {elo[m]:7.1f}  score-rate {winrate[m]:.2f}  ({games_played[m]} games)")


if __name__ == "__main__":
    main()
