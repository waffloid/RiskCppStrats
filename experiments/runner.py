"""Base runner for CRisky gym binaries (subprocess + CSV)."""

import subprocess
import os
import itertools
from pathlib import Path
from typing import Any

import pandas as pd

DEFAULT_BUILD_DIR = Path(__file__).resolve().parent.parent / "build"


class GymRunner:
    """Runs a gym binary with CLI args, reads output CSV."""

    def __init__(self, binary: str, output_dir: str = "output",
                 build_dir: Path | None = None):
        self.build_dir = build_dir or DEFAULT_BUILD_DIR
        self.binary = self.build_dir / binary
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def run(self, output_file: str = "results.csv",
            **kwargs: Any) -> pd.DataFrame:
        """Run binary once with kwargs as --key=value args."""
        output_path = self.output_dir / output_file
        args = [str(self.binary)]
        args.append(f"--output={output_path}")
        for k, v in kwargs.items():
            args.append(f"--{k}={v}")

        result = subprocess.run(args, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(
                f"{self.binary.name} failed (rc={result.returncode}):\n"
                f"{result.stderr}")

        return pd.read_csv(output_path)

    def sweep(self, param_grid: dict[str, list[Any]],
              output_prefix: str = "sweep",
              **fixed: Any) -> pd.DataFrame:
        """Run across parameter grid, concat results."""
        keys = list(param_grid.keys())
        combos = list(itertools.product(*param_grid.values()))
        frames = []

        for i, combo in enumerate(combos):
            params = dict(zip(keys, combo))
            params.update(fixed)
            tag = "_".join(f"{k}={v}" for k, v in zip(keys, combo))
            out_file = f"{output_prefix}_{tag}.csv"
            df = self.run(output_file=out_file, **params)
            for k, v in zip(keys, combo):
                df[k] = v
            frames.append(df)

        return pd.concat(frames, ignore_index=True)
