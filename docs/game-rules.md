# Game Rules & Engine Mechanics

How the simulation actually works, as implemented in `src/engine/` (plus combat in `src/systems/combat/`). All constants live in `GameConfig` (`src/engine/game_config.hpp`).

## The tick loop

`Game::tick(dt, commands)` (`src/engine/game.cpp`) executes in this exact order:

1. **Builds** — `process_build_commands`
2. **Troop sends** — `process_troop_sends`
3. **Retreats** — `process_retreats`
4. **Edge lanes** — advance groups, aggregate, resolve collisions, deposit arrivals
5. **Combat** — `resolve_all_combat(dt)`
6. **Ownership** — `update_ownership`
7. **Production** — `produce_all_troops(dt)`
8. **Alive check** — `update_alive`

AI/human players implement `PlayerInterface::decide(game, player_id, PlayerCommands&)` (`src/engine/player_interface.hpp`) and emit three command types: `TroopCommand{from, to, count}` (where `to` is a *global* destination, not a neighbor), `BuildCommand{node, structure}`, `RetreatCommand{node}`.

## Structures & production

| Structure | Cost | Effect |
|---|---|---|
| Capital | — (starting node) | produces 2 troops/tick |
| Factory | 500 | produces 1 troop/tick |
| Powerplant | 2500 | +2 troops/tick to each **adjacent** owned factory/capital (bonuses stack) |
| Fort | 400 | owner's defense ×1.2 at that node |
| Artillery | 4000 | owner's attack ×1.5 at **neighboring** nodes |

Build validation: node must be owned, not a capital, not already that type, and the node must retain ≥1 troop after paying. Production accumulates `dt` and fires once per whole 1.0 of game time, so the production *rate* is invariant to game speed (`produce_all_troops`).

## Troop movement (edge lanes)

Each edge has two directed lanes (`src/engine/edge_lanes.cpp`). A `TroopGroup` tracks owner, count, position ∈ [0,1], and its global destination.

- **Speed**: `speed = 1/max(1, count) + min_troop_speed` world-units/sec — *smaller groups move faster*. (Default `min_troop_speed = 0.1`.)
- **Aggregation**: same-owner, same-direction groups merge when the trailing (faster) group catches up within a radius-based gap.
- **Collisions**: opposing groups that cross on an edge — the larger group wins; the smaller is flagged `forced_retreat` and turns back. Equal counts tie-break deterministically by node parity.
- **Retreats**: a retreating group returning to its origin deposits there with its routing forgotten. A `RetreatCommand` flips all of a player's outbound groups on adjacent edges.
- **Counter-flow rule**: sending troops onto an edge where your own troops are inbound flips those to retreating instead of dispatching (prevents self-collision), and skips the send that tick.

**Routing** (`src/engine/routing.cpp`): `next_hop` is a greedy dot-product heuristic — pick the neighbor whose direction best aligns with the straight line to the global destination. Re-applied at every intermediate node on arrival, so multi-hop paths are re-planned hop by hop.

## Combat

Combat is a pure function (`src/systems/combat/combat_resolver.{hpp,cpp}`); `CombatState` carries fractional damage between ticks, separate from `NodeData`.

At any node where ≥2 players have troops:

- `attack[p] = troops[p] / attack_divisor` (default 100)
- `defense[p] = troops[p] / defense_divisor` (default 1000)
- Fort: node owner's defense ×`fort_defense_mult` (1.2)
- Artillery on a neighboring node: that owner's attack here ×`artillery_attack_mult` (1.5)
- Per player: `loss = max(Σ other players' attack − own defense, 0)`, scaled by `dt/base_dt`, accumulated fractionally; the integer part becomes casualties.

## Ownership, players, winning

- A node is owned by the *sole* player with troops on it; 0 or ≥2 players present → owner −1 (empty/contested).
- If `init_default_troops > 0`, a **neutral player** occupies every non-capital node at start (passive; must be fought through).
- A player is alive while they have troops on any node or edge.
- **Win condition: last real player standing** (`Game::is_game_over`, neutral excluded). The design notes and the original Roblox game describe a 70%-of-map victory threshold — that is *not implemented*; games end by elimination or the caller's tick limit.

`Game::effective_troops(player)` gives per-node counts *including* in-transit groups, interpolated by edge position — used by later AI models to avoid double-sending.

## Graph generation

`Graph::generate_poisson(config, seed)` (`src/engine/graph.cpp`):

1. Node count ~ Poisson(`poisson_intensity` × area); points sampled uniformly, optionally restricted to an inscribed circle (`circular`) with `num_holes` random circular holes.
2. Edges connect all pairs within `edge_distance_threshold` (Euclidean length stored).
3. `cull_high_degree` removes tightly-clustered high-degree nodes until all degrees ≤ `max_neighbors`.
4. Nodes reordered by angle around the centroid for spatial locality; `pick_spaced_capitals(n)` places capitals at evenly-spaced angles.

Explicit constructors for tests/benchmarks in `graph_builder.cpp`: `build_graph`, `build_bipartite(n,m)`, `build_path(n)`, `build_star(n)`.

Use `graph.neighbors(i)` / `graph.degree(i)` — not `nodes[i].neighbor_indices` directly.

## GameConfig defaults

| Group | Fields (defaults) |
|---|---|
| Graph | `poisson_intensity` 0.005, `region_width/height` 100, `edge_distance_threshold` 20, `max_neighbors` 6, `circular` false, `num_holes` 0 |
| Troops | `init_troop_count` 501 (capital), `init_default_troops` 0 (neutral off), `min_troop_speed` 0.1 |
| Combat | `attack_divisor` 100, `defense_divisor` 1000, `fort_defense_mult` 1.2, `artillery_attack_mult` 1.5, `base_dt` 1.0 |
| Production | `capital_troops_per_tick` 2, `factory_troops_per_tick` 1, `powerplant_bonus` 2 |
| Costs | factory 500, powerplant 2500, fort 400, artillery 4000 |
| Lanes | `radius_factor` 0.01, `aggregation_buffer` 1.0 |

(The interactive game and headless runner override the graph parameters — intensity 0.16, region 158², threshold 10, max neighbors 7, circular with 6 holes, neutral troops 25.)
