# The AI: Distribution Pipeline & Model Lineage

Code: `src/ai/`. Solver internals are documented in [solvers.md](solvers.md).

## Design philosophy

Rather than parameterizing raw state/actions and throwing RL at the whole game, the game is decomposed into subproblems, each attacked with the right specialized machinery:

- **Building** — what configuration of factories/powerplants do I want? (max-cut / QUBO / annealing)
- **Transport** — given desired troop targets, how do I move troops there fastest? (optimal transport / min-cost flow, or potential flow)
- **Combat** — how do I allocate troops along a battlefront? (greedy/knapsack today; honestly a control problem, still open)

The bet is that many aspects can be solved *optimally* with classical solvers and shouldn't be learned, and that engaging with the structure identifies which surfaces irreducibly need ML. See the design notes (Risky.pdf) for the full argument, including why transport optimizes *average* arrival time (Kantorovich) and how the three OT mismatches (unknown targets, unbalanced mass, ignored production) are resolved with need-based inequality demands, value-weighted costs, and saturating production tranches.

## The decision pipeline

`DistributionAIPlayer` (`src/ai/players/distribution_ai_player.{hpp,cpp}`) orchestrates weighted sub-agents. Each tick, `decide()` runs:

1. **Score** — each sub-agent fills a per-node score array and may emit *direct commands* (builds, attacks, retreats) that bypass the distribution entirely.
2. **Combine** — each agent's scores are max-normalized to [0,1], then summed weighted by the agent's pool weight (`economy_pool_weight` 5, `expansion_pool_weight` 1, `war_pool_weight` 1). "Linear-combine-then-softmax."
3. **Suppress zeros** — unscored nodes get −1e6 so hundreds of irrelevant nodes don't dilute the softmax.
4. **Softmax** — `softmax(combined, global_beta=3)` → target troop distribution. With `use_distance_softmax`, a distance-decayed local softmax is used instead (nodes compete locally, using a lazily-built all-pairs BFS hop matrix).
5. **Potential field** — `deficit[i] = target_fraction[i]·total_troops − current[i]` (pluggable `PotentialSolver`, default `potential_deficit`), then a graph Poisson solve `L·φ = deficit` via Gauss–Seidel/SOR, warm-started from the previous tick.
6. **Merge direct commands** — war-committed source nodes are masked so transport won't override them.
7. **Transport** — one of two paths:
   - **Greedy (default)**: `transport_solver_greedy` moves troops down the potential gradient with a sigmoid-scaled outflow fraction.
   - **OT (`enable_ot_transport()`)**: builds need-based demands and solves min-cost flow on the game graph via `NetworkSimplex` (warm-started, Frank–Wolfe saturation). Demands: build targets get their structure cost, other owned nodes a minimum garrison, scored unowned nodes `defenders+1`; with `ot_frontline_garrison > 0`, owned border nodes demand 1.2× the strongest enemy neighbor. Demand values weight edge costs so high-value targets are served first; `enable_effective_troops()` subtracts in-transit troops from demand.

A note on smoothing: `ema()` and `ema_alpha` exist (`src/ai/distribution.cpp`) but are **not applied** in the current decide path — the target distribution can jump tick to tick.

## Sub-agents

All implement `DistributionSubAgent` (`src/ai/distribution_sub_agent.hpp`); files in `src/ai/sub_agents/`.

- **EconomySubAgent** — On first tick, computes a global "ideal plan" *once* by running an economy solver on the map as if the player owned everything (default MCMC annealing; QUBO models inject a different solver). Each tick it rebuilds a build queue from the plan: nearest-to-infrastructure first, powerplants gated until ≥3 adjacent producers are built. Scores queue nodes (factory `3·(1+owned_nbrs)`, powerplant 8, decaying by queue rank); emits a direct build the moment a target node's troops exceed the structure cost ("parasitic building"). Exposes `build_queue()` — the OT transport path reads it to derive per-node demand costs.
- **ExpansionSubAgent** — scores unowned border nodes `expansion_border_score × owned_neighbors`. Pure distribution pull; no direct commands.
- **ConditionalExpansionSubAgent** — same, but goes silent whenever more than `expansion_unconstructed_cutoff` of owned nodes are still unconstructed (forces building before expanding).
- **DirectWarSubAgent** — pulls reserves toward the front (`war_front_line_score × target_value` on owned neighbors of enemy targets) and emits direct attacks via `v2_solve_attacks` plus auto-retreats for in-flight groups heading at targets that are no longer committed attacks.
- **KnapsackWarSubAgent** — same attack path; additionally computes greedy-vs-optimal knapsack benchmarking metrics (`knapsack_ratio`). Used by v1 models.

War target value/cost/priority helpers live in `src/ai/utils/war_utilities.{hpp,cpp}`: value from structure type + powered adjacency, cost inflated by forts, priority `value/(degree/K + 1)`, sends sourced from lowest-degree frontier nodes first, with in-flight accounting.

## ModelConfig

`src/ai/model_config.hpp` centralizes every tunable (GA tuning mutates this struct directly). Key fields: `global_beta` 3.0, pool weights (5/1/1), war capture values (capital 5, artillery 3, powerplant 2, factory 1), `transport_outflow_rate` 0.15, `use_distance_softmax` false, and the OT knobs `ot_saturation_alpha` 0.005, `ot_value_alpha` 0.0, `ot_fw_iterations` 8, `ot_frontline_garrison` 0.

## Model lineage (v0 → v12)

Models are registered by name in `src/ai/models/` (`get_model(name)` / `list_models()`; QUBO-dependent models register via `register_graph_algo_models()`). Each is `DistributionAIPlayer` with an explicit agent set. Delta from predecessor:

| Model | Change |
|---|---|
| `v0_expansion` | Economy (MCMC) + Expansion. No war. |
| `v1_knapsack` | + KnapsackWar, *drops* Expansion. |
| `v1_knapsack_hybrid` | All three agents; expansion weight 0.5. |
| `v2_knapsack` | DirectWar replaces KnapsackWar. |
| `v3`, `v4` | Identical to v2 — kept as benchmark anchors from before the config-defaults rebalance. |
| `v5_qubo` | Economy solved by QUBO SA ("production" objective) instead of MCMC. (`v5_qubo_composite`: "composite" objective.) |
| `v6` | QUBO "factory_biased" objective (bias 2.5, targets ~70–80% factories). Verified better via gym_economy. |
| `v7` | v6 + ConditionalExpansion (cutoff 0.2). |
| `v8` | v6 + distance softmax. |
| `v9` | QUBO "production" with Goemans–Williamson rounding + **OT transport** (network simplex). First OT model. |
| `v10` | v9 + ConditionalExpansion (cutoff 0.2). |
| `v11` | v10 + effective (in-transit) troops in OT demand. |
| `v12` | v11 + frontline garrison pooling (`ot_frontline_garrison = 50`). |

## Observability

`src/observability/` — `AIDecisionSnapshot` captures the whole pipeline per tick (per-agent raw scores, combined, smoothed, gradient, troops) for the viz panels; `MetricsCollector` computes per-player game metrics (production, territory, frontier perimeter, K/D) as a read-only observer; `MetricsWriter` streams JSON-lines and prints mean/stdev summaries. `crisky_game`'s F1 debug panels and the `_viz` gyms are fed from these.
