# Tools: Binaries, Gyms & Experiments

Everything you can build and run. Entry points live in `src/apps/`, gym logic in `src/gyms/`, Python orchestration in `experiments/`.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

CMake 3.20+, C++20. RayLib, ImGui/ImPlot, and Eigen are fetched automatically (Eigen via manual FetchContent populate to avoid a target clash with raylib).

Tests — 17 plain-assert binaries, all must pass before commits:

```sh
for t in build/test_*; do ./$t || exit 1; done
```

## `crisky_game` — interactive game

```sh
./build/crisky_game [seed] [flags]
```

| Flag | Meaning |
|---|---|
| `[seed]` | first positional arg (default 42) |
| `--human` | you control player 0 (default: AI-vs-AI spectator) |
| `--players=N` | player count ≥2 (default 3) |
| `--model=NAME` | AI model per slot, repeatable; unknown name prints the registry |
| `--scheme=NAME\|N` | color scheme (17 available) |
| `--scenario=buildup\|bipartite` | preset test scenarios instead of the Poisson map |
| `--record[=file.mp4]`, `--record-delay=S` | record via ffmpeg pipe |

One game tick per frame (`dt = 0.25` × speed) at 60 FPS.

**Controls**: click / shift-click / alt-click select, drag circle-select; hover + `Q`/`E` send 501/2501, `R`/`F` send 50%/100%; `1–4` build factory/fort/powerplant/artillery; `WASD` pan, `I`/`O` zoom, `K`/`L` rotate; `+`/`-` speed (0.25–64×), `Space` pause; `[`/`]` themes, `Z` zen mode, `F1` ImGui debug panels, `H` heatmap overlay, `G` gradient arrows, `Tab` cycle inspected AI.

## `crisky_headless` — no-GUI simulation

```sh
./build/crisky_headless [seed] [max_ticks] [model0 model1 ...] [--json] [--diag[=N]] [--dt=F]
```

Defaults: seed 42, 10000 ticks, two `v0_expansion`. `--json` emits machine-readable state every 100 ticks and at game over; `--diag` dumps buildings, top distribution nodes, and AI entropy/concentration metrics. Prints ticks/sec and the winner.

## Gyms

Headless experiment harnesses, CSV output under `output/<gym>/` via `DataSink`. All accept `--help`.

| Binary | What it measures | Key flags |
|---|---|---|
| `gym_economy` | Solver production efficiency vs theoretical max on a random graph the player fully owns | `--solver=greedy\|bootstrap\|mcmc\|qubo\|qubo_<objective>`, `--seed`, `--runs`, `--nodes` |
| `gym_combat` | Two models fight on a benchmark map (building disabled) | `--solver`, `--opponent`, `--benchmark=corridor\|bipartite_3_5\|star_hub\|grid_5x5`, `--spartan=X` (scales opponent troops), `--per-tick=PATH` |
| `gym_transport` | Ticks for a transport solver to converge to a target distribution (no production/combat) | `--preset=star_center\|path_end\|bipartite_split\|poisson_*`, `--solver=greedy\|ot`, `--loss=l1\|l2\|max_deficit` |
| `gym_ordering` | Time-to-build and accumulated production of a build order | `--economy`, `--ordering`, `--seed`, `--runs` |
| `gym_maxcut` | QUBO objective × ordering sweep (production + efficiency) | `--objective=NAME\|all`, `--ordering=NAME\|all`, `--nodes`, `--production-csv` |
| `gym_composed` | Economy→Ordering→Transport pipeline demo, single player | `--economy`, `--ordering`, `--transport`, `--ticks` |
| `gym_transport_diag` | Per-node oscillation trace on `path_end`, CSV to stdout | none |

Interactive `_viz` variants (raylib + ImGui, shared `VizApp` shell with pause/speed/playback): `gym_combat_viz`, `gym_transport_viz` (press `M` for mouse-follow targets; supply-Voronoi heatmap in OT mode), `gym_economy_viz` (live MCMC annealing with in-app graph/temperature controls), `gym_maxcut_viz` (live SA on the QUBO), `gym_joint_economy_viz` (joint cheap/expensive QUBO with live μ sliders).

`src/gyms/projections.{hpp,cpp}` can also project a *live* `Game` into gym state (`project_economy`, `project_combat`, `project_transport`) so the same tooling works mid-game.

## Other binaries

- `ga_tune` — genetic algorithm over the three sub-agent pool weights; round-robin tournaments, hardcoded parameters (pop 20, 100 generations).
- `crisky_screenshots` — simulates 2000 ticks then renders one PNG per color scheme into `screenshots/`.
- `viz_test` — ImGui/ImPlot/RingBuffer smoke test, no game logic.

## Python experiments (`experiments/`)

Each C++ binary is a standalone Unix tool; Python composes them via subprocess and reads their CSV/JSONL.

- `runner.py` / `game_runner.py` — `GymRunner` and `GameRunner` (headless `--json` parsing) bases with `sweep()`.
- `economy.py`, `combat.py`, `transport.py`, `ordering.py`, `maxcut.py` — per-gym experiment suites (solver comparisons, benchmark batteries, spartan sweeps, objective × ordering sweeps with matplotlib plotting).
- `elo_tournament.py` — parallel Elo tournament across the model registry via `crisky_headless` (`--rounds`, `--batch`, `--dt`).

## Repo odds and ends

- `PyRisky/` — the original Python prototype of the game (matplotlib/vispy visualizers, the attention-gradient AI that the C++ distribution pipeline descends from). Superseded by the C++ engine; kept for reference.
- `papers/` — reference PDFs (network flows, Hamilton–Jacobi, MCMC/spin systems) behind the transport and economy solvers.
- `claude/` — working design notes (knapsack war solver design, AI session notes).
- `output/` — gitignored experiment CSVs.
- `src/player/` — empty leftover directory from the pre-overhaul layout (the AI moved to `src/ai/`).
