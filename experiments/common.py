"""Shared infrastructure for experiment runners.

Every experiment writes into results/<name>/:
  manifest.json   -- git SHA, date, command, parameters (reproducibility record)
  raw/*.csv       -- unaggregated per-game / per-run data
  tables/*.tex    -- generated booktabs tables, \\input by writeup/
  summary.csv     -- the aggregated ranking
"""

import csv
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
RESULTS_ROOT = REPO_ROOT / "results"
BUILD_DIR = REPO_ROOT / "build"


def git_sha():
    out = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    return out.stdout.strip() or "unknown"


def results_dir(name):
    d = RESULTS_ROOT / name
    (d / "raw").mkdir(parents=True, exist_ok=True)
    (d / "tables").mkdir(parents=True, exist_ok=True)
    return d


def write_manifest(outdir, params):
    manifest = {
        "git_sha": git_sha(),
        "date_utc": datetime.now(timezone.utc).isoformat(),
        "command": " ".join(sys.argv),
        "python": platform.python_version(),
        "platform": platform.platform(),
        "params": params,
    }
    with open(Path(outdir) / "manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
    return manifest


def write_csv(path, header, rows):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def tex_escape(s):
    return str(s).replace("_", r"\_").replace("%", r"\%").replace("#", r"\#")


def write_tex_table(path, header, rows, colspec=None, caption=None, label=None):
    """Emit a booktabs table body (or full table env if caption given)."""
    colspec = colspec or "l" + "r" * (len(header) - 1)
    lines = []
    if caption:
        lines += [r"\begin{table}[ht]", r"\centering"]
    lines.append(rf"\begin{{tabular}}{{{colspec}}}")
    lines.append(r"\toprule")
    lines.append(" & ".join(tex_escape(h) for h in header) + r" \\")
    lines.append(r"\midrule")
    for row in rows:
        lines.append(" & ".join(tex_escape(c) for c in row) + r" \\")
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    if caption:
        lines.append(rf"\caption{{{caption}}}")
        if label:
            lines.append(rf"\label{{{label}}}")
        lines.append(r"\end{table}")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n")


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")


def stdev(xs):
    xs = list(xs)
    if len(xs) < 2:
        return 0.0
    m = mean(xs)
    return (sum((x - m) ** 2 for x in xs) / (len(xs) - 1)) ** 0.5
