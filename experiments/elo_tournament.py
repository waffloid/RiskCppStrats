#!/usr/bin/env python3
"""Elo tournament orchestrator for CRisky bots.

Randomly samples bot pairings, runs games in parallel,
aggregates fractional winrates for duplicate pairings, and refines Elo ratings.

Usage:
    python3 experiments/elo_tournament.py [--rounds=30] [--batch=16] [--dt=16]
"""

import subprocess
import re
import random
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed

# ── Config ──────────────────────────────────────────────────────────
HEADLESS = "./build/crisky_headless"
MODELS = [
    "v0_expansion", "v1_knapsack", "v1_knapsack_hybrid",
    "v4", "v5_qubo", "v6", "v7", "v8",
    "v9", "v10", "v11", "v12",
]
MAX_TICKS = 5000
K = 32.0
INITIAL_ELO = 1500.0


# ── Game runner ─────────────────────────────────────────────────────

def run_game(model_a, model_b, seed, dt):
    """Run model_a (solo) vs model_b x2. Returns score for A: 1.0/0.5/0.0."""
    pos = seed % 3
    slots = [model_b, model_b, model_b]
    slots[pos] = model_a

    cmd = [HEADLESS, str(seed), str(MAX_TICKS)] + slots + [f"--dt={dt}"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=120).stdout
    except subprocess.TimeoutExpired:
        return None

    for line in reversed(out.strip().split('\n')):
        if line.startswith("tick"):
            nodes = {}
            for m in re.finditer(r'P(\d): \d+ troops, (\d+) nodes', line):
                nodes[int(m.group(1))] = int(m.group(2))
            a_n = nodes.get(pos, 0)
            b_best = max((nodes.get(i, 0) for i in range(3) if i != pos), default=0)
            if a_n > b_best:
                return 1.0
            if a_n < b_best:
                return 0.0
            return 0.5

    return None


# ── Sampling ────────────────────────────────────────────────────────

def sample_matchups(n):
    """Sample n random (solo, duo) pairs uniformly."""
    result = []
    for _ in range(n):
        a, b = random.sample(MODELS, 2)
        result.append((a, b))  # a=solo, b=duo
    return result


# ── Main loop ───────────────────────────────────────────────────────

def main():
    num_rounds = 30
    batch = 16
    dt = 16

    for arg in sys.argv[1:]:
        if arg.startswith("--rounds="):
            num_rounds = int(arg.split("=")[1])
        elif arg.startswith("--batch="):
            batch = int(arg.split("=")[1])
        elif arg.startswith("--dt="):
            dt = int(arg.split("=")[1])

    random.seed(42)
    elos = {m: INITIAL_ELO for m in MODELS}
    game_seed = 1
    total = 0
    games_per_model = defaultdict(int)

    n_pairs = len(MODELS) * (len(MODELS) - 1) // 2
    print(f"Elo Tournament: {len(MODELS)} bots, {n_pairs} possible pairs")
    print(f"  {num_rounds} rounds x {batch} games, dt={dt}, K={K}")
    print(f"  Models: {', '.join(MODELS)}")
    print()

    for rnd in range(1, num_rounds + 1):
        matchups = sample_matchups(batch)

        # Launch all games in parallel
        futures = {}
        with ProcessPoolExecutor(max_workers=batch) as ex:
            for solo, duo in matchups:
                f = ex.submit(run_game, solo, duo, game_seed, dt)
                futures[f] = (solo, duo)
                game_seed += 1

        # Aggregate by unordered pair: track score for lexicographically-first model
        pair_agg = defaultdict(lambda: [0.0, 0])  # (a, b) -> [score_for_a, n]
        for f in as_completed(futures):
            solo, duo = futures[f]
            score = f.result()
            if score is None:
                continue
            key = tuple(sorted([solo, duo]))
            a, _ = key
            score_for_a = score if solo == a else (1.0 - score)
            pair_agg[key][0] += score_for_a
            pair_agg[key][1] += 1
            total += 1
            games_per_model[solo] += 1
            games_per_model[duo] += 1

        # One Elo update per unique pair with fractional winrate
        for (a, b), (total_s, n) in pair_agg.items():
            frac = total_s / n
            ea = 1.0 / (1.0 + 10.0 ** ((elos[b] - elos[a]) / 400.0))
            elos[a] += K * (frac - ea)
            elos[b] += K * ((1.0 - frac) - (1.0 - ea))

        # Print round summary
        ranked = sorted(elos.items(), key=lambda x: -x[1])
        pairs_str = " | ".join(
            f"{a} vs {b} ({t_s/n:.0%})" for (a, b), (t_s, n) in pair_agg.items()
        )
        print(f"R{rnd:2d}/{num_rounds} [{total} games]  {pairs_str}")
        for name, elo in ranked:
            delta = elo - INITIAL_ELO
            bar = "#" * max(0, int((elo - 1400) / 5))
            print(f"  {name:25s} {elo:7.1f} ({'+' if delta >= 0 else ''}{delta:4.0f}) {bar}")
        print()

    # Final output
    print("=" * 60)
    print("FINAL ELO RATINGS")
    print("=" * 60)
    ranked = sorted(elos.items(), key=lambda x: -x[1])
    for i, (name, elo) in enumerate(ranked):
        gp = games_per_model.get(name, 0)
        print(f"  {i+1:2d}. {name:25s} {elo:7.1f}  (games: {gp})")
    print(f"\n{total} games played across {num_rounds} rounds")


if __name__ == "__main__":
    main()
