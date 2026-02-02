# CRisky — Project Summary

## What It Is

CRisky is a discrete-time strategy game played on randomly generated graphs embedded in 2D space. Players control troops across nodes, build structures (factories, powerplants, forts, artillery), and fight for territorial control. Think Risk but on graph topology with continuous troop movement along edges. Designed both as a playable game and as a testbed for reinforcement learning agent training.

## Architecture

- **C++20**, built with CMake, rendering via **RayLib 5.5**
- The engine (`src/engine/`) is a standalone static library with zero external dependencies (STL only) — can be embedded elsewhere or used headlessly for RL training
- A separate renderer (`src/renderer/`) provides interactive visualization via RayLib
- A headless runner (`src/main_headless.cpp`) exists for RL/automated play

## File Layout

```
src/engine/
  game_config.hpp    - 50+ tunable parameters
  game_state.hpp     - NodeState enum, NodeData struct
  graph.hpp/cpp      - Poisson graph generation, culling, edge management
  edge_lanes.hpp/cpp - TroopGroup movement, collision, aggregation on edges
  combat.hpp/cpp     - Combat resolution with fort/artillery modifiers
  production.hpp/cpp - Troop generation from structures
  routing.hpp/cpp    - Dot-product greedy pathfinding
  game.hpp/cpp       - Main Game class, 8-step tick loop

src/renderer/
  camera.hpp/cpp     - Custom 2D camera (zoom, pan, fit-to-graph)
  renderer.hpp/cpp   - Graph/troop visualization

src/player/
  player_interface.hpp - Abstract PlayerInterface base, command structs

src/main_game.cpp      - Interactive RayLib demo (spectator mode)
src/main_headless.cpp  - Headless runner for RL/testing

tests/
  test_graph.cpp       - Graph generation, culling, determinism
  test_edge_lanes.cpp  - Movement, collision, aggregation, retreat
  test_combat.cpp      - Combat math, fort/artillery effects
  test_game.cpp        - Integration: construction, sending, determinism
  test_gameplay.cpp    - Gameplay scenarios: ownership, capture, production
```

## Core Mechanics

- **Graph generation**: Poisson process samples nodes, connects nearby ones by distance threshold, iteratively culls over-connected nodes
- **Troop movement**: Troops travel along edge "lanes" with position 0–1. Bigger armies move slower (strategic tradeoff). Groups can collide on edges, forcing the smaller one to retreat (no troop losses)
- **Aggregation**: Same-owner, same-direction groups within radius merge automatically
- **Combat**: Attrition-based. `loss_i = max(sum_{j!=i} attack_j - defense_i, 0)`. Forts boost defense (1.2x), artillery boosts attack at neighboring nodes (1.5x). All integer arithmetic
- **Production**: Capital +2/tick, Factory +1/tick, Powerplant +2 bonus to adjacent producers owned by same player
- **Building**: Costs paid in troops (Factory 500, Powerplant 2500, Fort 400, Artillery 4000). Must retain at least 1 troop to keep ownership
- **Routing**: Greedy dot-product heuristic for multi-hop pathfinding. Works well for convex graphs
- **Ownership**: Node owned by player P iff P is the sole player with troops there

## Game Loop (8 steps per tick)

1. Process build commands
2. Send troops (route first hop, insert into edge)
3. Process retreats
4. Advance/collide/aggregate on edges
5. Process arrivals (deposit or reroute)
6. Resolve combat, then update ownership
7. Produce troops
8. Check eliminations

## Current State

### Working

- Full engine with all core mechanics
- Comprehensive test suite (5 test files, all passing)
- Interactive RayLib visualization with camera controls
- SimpleAI demo (sends surplus troops, builds factories)
- Headless runner for automated play
- Deterministic game state (fixed seed = identical results)

### Not Yet Implemented

- **Human player input** — UI is spectator-only, no click-to-send or selection
- **Advanced AI** — attention-mechanism RL agent is designed but not built
- **UI features** — fog of war, circle-drag selection, hotkeys, hover highlighting
- **Optimization** — no spatial hashing (O(n^2) edge detection), no GPU acceleration
