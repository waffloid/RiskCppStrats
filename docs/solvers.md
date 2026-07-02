# Solvers

The extracted, independently-testable solver layer in `src/systems/`. Contract types (`BuildPlan`, `DesiredDistribution`, `TroopDemand`) live in `src/systems/common/types.hpp`; cyclic couplings use the double-buffered `Buffer<T>` (`common/buffer.hpp`); gym CSV output goes through `DataSink` (`common/data_sink.hpp`).

Registry pattern: `get_transport_solver`, `get_ordering_solver`, `get_combat_solver`, `get_qubo_solver`, `get_qubo_objective` each take a name and have a matching `list_*`. Economy solvers are free functions (the string registry for them lives at the gym level, `src/gyms/economy_gym.cpp`). Known quirk: `list_transport_solvers()` advertises `"ot"` but `get_transport_solver` only builds `"greedy"` — OT solvers are constructed via `make_ot_solver` / `NetworkSimplex` directly.

## Economy (`systems/economy/economy_solvers.{hpp,cpp}`)

Objective: maximize steady-state production, `compute_production_rate` — capital/factory base rates plus `powerplant_bonus` per adjacent owned powerplant, bonuses stacking. Exact optimum is NP-hard (a MAX-CUT variant); `compute_theoretical_max_production` gives a loose upper bound.

- **`economy_solver_greedy`** — per-node local rule: build a powerplant where factory-neighbors outnumber powerplant-neighbors (if affordable), else a factory. Only emits immediately-affordable builds.
- **`economy_solver_bootstrap`** — BFS ring of N factories around the capital, then one well-connected powerplant. Opening-book behavior.
- **`economy_solver_mcmc`** (+ `_traced`, + incremental `MCMCStepper` for viz) — simulated annealing over {DEFAULT, FACTORY, POWERPLANT} per node; greedy warm start; Metropolis acceptance on production delta; linear cooling (the stepper uses exponential). Reaches ~87–98% of theoretical max from warm start.
- **`economy_solver_qubo`** (`systems/graph_algo/qubo_objectives.cpp`) — encodes the layout as an Ising problem and delegates to the QUBO layer below; `solver_method` `"sa"` or `"gw"`.
- **`economy_solver_branch_bound`** — unimplemented stub.

## QUBO / max-cut layer (`systems/graph_algo/`)

Solves `max Σ h_i·x_i + Σ 2·J_ij·x_i·x_j`, `x ∈ {−1,+1}` (Ising form — *not* `xᵀQx`, whose diagonal is constant). Economy convention: +1 = factory, −1 = powerplant. Types: `QUBOInstance`, `QUBOSolution`, `QUBOTrace`, incremental `QUBOStepper`. Flip gains update incrementally in O(degree) (coefficient −8·Q·x_i·x_j).

**Solvers** (`get_qubo_solver`): `greedy` (hill-climb on flip gains), `sa` (annealing, greedy warm start, linear cooling), `gw` (Goemans–Williamson: rank-k projected-gradient SDP relaxation via Eigen, random-hyperplane rounding over 50 trials, greedy polish).

**Objectives** (`get_qubo_objective`) — Q-matrix builders over owned non-capital nodes:

| Name | Idea |
|---|---|
| `production` | Faithful encoding of the production math (factory linear preference; adjacency rewards opposite signs). |
| `adjacency` | Pure max-cut, no linear terms. |
| `distance` | Production scaled by 1/(1+BFS-distance-from-capital). |
| `degree` | Production + bias pushing high-degree nodes toward powerplant. |
| `composite` | Production + mild distance decay + mild degree bias. |
| `factory_biased` | Production + positive diagonal (strength 2.5 as registered) targeting ~70%+ factories. |

`qubo_objective_joint` builds a 2N-variable QUBO solving a *cheap* and an *expensive* layout jointly, with a bridge coupling rewarding agreement — explores plan-perturbation ("expand, then consolidate"). Visualized by `gym_joint_economy_viz`.

## Transport (`systems/transport/`)

Three generations of solver, all ending in `TroopCommand`s. In every case, per-source outflow is capped so ≥1 troop stays home, using proportional largest-remainder rounding.

### Potential flow (default path)
- `potential_deficit`: `deficit[i] = target_fraction[i]·total − current[i]`.
- `solve_graph_poisson`: Gauss–Seidel with SOR (ω=1.5) on `L·φ = deficit`, warm-started across ticks.
- `transport_solver_greedy`: each surplus node pushes a sigmoid-scaled fraction of its troops toward higher-potential neighbors, proportionally to the potential difference.

This is the Benamou–Brenier-flavored dynamical view from the design notes: treat the desired redistribution as a potential flow and descend the gradient.

### OT via successive shortest paths (`ot_solver.{hpp,cpp}`)
`OTSolver` builds a bipartite supply→demand min-cost-flow network over all-pairs Dijkstra distances (cached once per game) and solves it with SSP using Johnson potentials.

- **Saturation tranches**: when demand exceeds supply, at-target *producing* nodes are added as suppliers via parallel edges with capacity `production_rate × window` and increasing cost — future production competes with long-distance shipping.
- Costs are `dist` or `dist/demand_value`, so valuable targets are cheaper to serve.
- Flows are extracted from residual capacities *after* SSP terminates (incremental tracking breaks under reverse-edge cancellation); reduced costs are clamped ≥0 against FP noise; anti-parallel flows on an edge are netted out.
- Only **first-hop** commands are emitted from supply nodes; intermediate forwarding is handled by re-planning each tick. Full-path flows feed a supply-Voronoi diagnostic (`last_voronoi`).

### Network simplex + Frank–Wolfe (`network_simplex.{hpp,cpp}`) — current OT path
`NetworkSimplex` runs min-cost flow **directly on the game graph** (not bipartite), with all arcs preallocated once and zero heap allocation per pivot.

- Big-M artificial cold start; Dantzig pivoting switching to Bland's rule after 800 pivots to break degenerate cycling; warm-started between ticks.
- **Frank–Wolfe convex decomposition** replaces the piecewise saturation tranches: producer arc costs are the gradient of a quadratic `α·f²` penalty, re-solved and blended (`γ = 2/(k+2)`) for `ot_fw_iterations` rounds — the QP from the design notes ("cost saturates; solvable by a fixed-point method from traffic flow").
- Demand-side value bonuses (`ot_value_alpha`), effective-troops demand netting, masking of war-committed sources, Voronoi extraction, and diagnostics (`last_pivot_count`, `last_was_warm`).

Loss functions for the transport gym: `l1`, `l2`, `max_deficit` (`loss_functions.hpp`).

## Ordering (`systems/ordering/ordering_solvers.{hpp,cpp}`)

Given an unordered `BuildPlan`, return an execution permutation: `sequential` (identity), `cheapest_first`, `nearest_first` (BFS from capital), `production_gradient` (greedy: simulate each remaining build, commit the max production delta; O(n²) evaluations).

## Combat (`systems/combat/`)

- `combat_resolver.{hpp,cpp}` — the engine's attrition math as pure functions: `compute_node_casualties` / `resolve_all_node_combat` / `apply_combat_results`, with `CombatState` carrying fractional damage. See [game-rules.md](game-rules.md#combat) for the math.
- `combat_solvers.hpp` — the combat gym's "solver" slot is just the full model registry (`get_combat_solver(name)` → `get_model(name)`): whole AI models fight on benchmark maps. Actual attack decisions live in `v2_solve_attacks` (`src/ai/utils/war_utilities.cpp`): targets in priority order `value/(degree/K+1)`, attack when neighbor supply + in-flight exceeds cost, sourcing from low-degree nodes first.
