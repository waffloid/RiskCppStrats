#!/usr/bin/env python3
"""Single-game case studies: qualitative zoom-ins backing the ladder claims.

Runs a small set of named headless games under tournament conditions (dt=16,
5000 ticks, JSON telemetry every 100 ticks), stores the raw JSONL, and renders
troop/territory time-series figures. Games and seeds are fixed here so the
figures are exactly reproducible.

Usage: python3 -m experiments.case_studies.run
"""

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.common import BUILD_DIR, results_dir, write_manifest  # noqa: E402
from experiments.plotstyle import plt, PALETTE, style_axes  # noqa: E402

DT = 16
MAX_TICKS = 5000

# name -> (solo_model, duo_model, seed)   [solo occupies slot seed % 3]
# Seeds 1272 and 1301 are exact replays of tournament games (results/elo/raw/
# games.csv); "v12_counterfactual" re-plays v11's seed-1301 loss with v12
# substituted into the identical map and slot -- a single-variable comparison.
CASES = {
    "v9_beats_v8": ("v8", "v9", 1272),
    "v11_stall": ("v11", "v10", 1301),
    "v12_counterfactual": ("v12", "v10", 1301),
}


def run_case(solo, duo, seed):
    pos = seed % 3
    slots = [duo, duo, duo]
    slots[pos] = solo
    cmd = [str(BUILD_DIR / "crisky_headless"), str(seed), str(MAX_TICKS)] + slots \
        + [f"--dt={DT}", "--json"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=600).stdout
    ticks = [json.loads(l) for l in out.splitlines() if l.startswith("{")]
    return pos, ticks


def plot_case(name, solo, duo, pos, ticks, outpath):
    xs = [t["tick"] for t in ticks]

    def series(pid, key):
        return [next(p[key] for p in t["players"] if p["id"] == pid) for t in ticks]

    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.7))
    for ax, key, label in zip(axes, ["troops", "nodes"], ["Troops", "Nodes held"]):
        duo_ids = [i for i in range(3) if i != pos]
        # entity = model: solo gets slot-1 blue, both duo copies slot-2 aqua
        ax.plot(xs, series(pos, key), color=PALETTE[0], label=f"{solo} (solo)")
        ax.plot(xs, series(duo_ids[0], key), color=PALETTE[1], label=f"{duo} (a)")
        ax.plot(xs, series(duo_ids[1], key), color=PALETTE[1], linestyle="--",
                label=f"{duo} (b)")
        ax.set_xlabel("tick")
        ax.set_ylabel(label)
        style_axes(ax)
    axes[0].legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(outpath)
    plt.close(fig)


def main():
    outdir = results_dir("case_studies")
    write_manifest(outdir, {"cases": {k: list(v) for k, v in CASES.items()},
                            "dt": DT, "max_ticks": MAX_TICKS})
    figdir = outdir / "figures"
    figdir.mkdir(exist_ok=True)

    for name, (solo, duo, seed) in CASES.items():
        pos, ticks = run_case(solo, duo, seed)
        with open(outdir / "raw" / f"{name}.jsonl", "w") as f:
            for t in ticks:
                f.write(json.dumps(t) + "\n")
        plot_case(name, solo, duo, pos, ticks, figdir / f"{name}.pdf")
        last = ticks[-1]
        summary = ", ".join(f"P{p['id']}: {p['troops']}t/{p['nodes']}n"
                            for p in last["players"])
        print(f"{name}: seed={seed} solo={solo}@P{pos} final tick {last['tick']}  {summary}")


if __name__ == "__main__":
    main()
