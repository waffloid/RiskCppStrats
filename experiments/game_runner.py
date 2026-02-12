"""Runner for full CRisky games via crisky_headless --json."""

import json
import subprocess
from pathlib import Path
from typing import Any

import pandas as pd

DEFAULT_BUILD_DIR = Path(__file__).resolve().parent.parent / "build"


class GameRunner:
    """Runs crisky_headless --json, parses JSONL output."""

    def __init__(self, build_dir: Path | None = None):
        self.build_dir = build_dir or DEFAULT_BUILD_DIR
        self.binary = self.build_dir / "crisky_headless"

    def run_game(self, seed: int = 42, max_ticks: int = 10000,
                 model_p0: str = "v0_expansion",
                 model_p1: str = "v0_expansion") -> pd.DataFrame:
        """Run a single game, return per-snapshot DataFrame."""
        args = [str(self.binary), "--json",
                str(seed), str(max_ticks), model_p0, model_p1]
        result = subprocess.run(args, capture_output=True, text=True)
        if result.returncode not in (0,):
            raise RuntimeError(
                f"Game failed (rc={result.returncode}):\n{result.stderr}")

        rows = []
        for line in result.stdout.strip().split("\n"):
            if not line:
                continue
            snap = json.loads(line)
            row: dict[str, Any] = {"tick": snap["tick"],
                                   "game_over": snap["game_over"]}
            for p in snap["players"]:
                pid = p["id"]
                row[f"p{pid}_troops"] = p["troops"]
                row[f"p{pid}_nodes"] = p["nodes"]
                row[f"p{pid}_alive"] = p["alive"]
            rows.append(row)

        return pd.DataFrame(rows)

    def tournament(self, models: list[str], seeds: list[int],
                   max_ticks: int = 10000) -> pd.DataFrame:
        """Round-robin tournament. Returns per-matchup results."""
        results = []
        for i, m0 in enumerate(models):
            for j, m1 in enumerate(models):
                if i == j:
                    continue
                for seed in seeds:
                    df = self.run_game(seed, max_ticks, m0, m1)
                    final = df.iloc[-1]
                    winner = None
                    if final["game_over"]:
                        if final.get("p0_alive", False):
                            winner = m0
                        elif final.get("p1_alive", False):
                            winner = m1
                    results.append({
                        "seed": seed,
                        "model_p0": m0,
                        "model_p1": m1,
                        "winner": winner,
                        "ticks": int(final["tick"]),
                        "game_over": final["game_over"],
                    })

        return pd.DataFrame(results)
