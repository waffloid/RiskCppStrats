# Session 0001: Design alignment
Date: 2026-02-02

## Done
- Read through existing skeleton code (graph.hpp, player.hpp, risky_game.hpp, risky_game.cpp)
- Catalogued bugs in existing code (bad `new`, reference binding, wrong clamp direction, int coordinates)
- Walked through Joel's design notes and resolved ambiguities
- Updated CLAUDE.md from inherited CFD project template to CRisky game design reference

## Decisions

| Decision | Verdict | Reason |
|----------|---------|--------|
| Graph type | Poisson process (not perturbed grid) | Joel's preference; perturbed grid dropped |
| Graph culling | Cull nodes with too many neighbors (not edges) | Corrected from original notes |
| Troops type | int (not float) | Joel confirmed |
| Combat formula sign | loss_i = max(sum_{j!=i} atk_j - def_i, 0) | Original notes had sign error |
| Displacement formula | `min(dt, (0.1 + 1/count) * dt / 0.2)` = `min(dt, (0.5 + 5/count) * dt)`. Per-tick displacement, not speed. Bigger army = slower. | Original from dev; the `0.5 + 5/count` constants were correct, Joel was misinterpreting as speed |
| sqrt travel time formula | Wrong, discard | Joel: "the sqrt formula is WRONG" |
| Edge collisions | Bigger group forces smaller to retreat, no troop loss. Forced flag prevents re-forcing (no infinite bouncing). First forcing wins if pressed from both sides. | Joel confirmed: "they just turn around" |
| Edge troop DSA | Per-edge, two sorted lanes (one per direction). Monotone stack optimization: only check aggregation where trailing group is smaller (faster) than leading group (slower). | Bigger = slower means smaller trailing catches larger leading, not vice versa |
| Fort/artillery multipliers | 1.2x defense, 1.5x attack (parameterized) | Default values, tunable |
| AI implementation | Deferred | Joel: "we should maybe note that in a file but we won't be doing this now" |
| Capital vs flag | `[MULL]` | Joel: "I dont think this really matters at this stage" |

## Open parameters
All tunable, no fixed values yet:
- Poisson intensity, region size, edge distance threshold, max neighbor count
- Displacement constants (C1=0.5, C2=5 in `min(dt, (C1 + C2/count)*dt)`)
- Aggregation radius factor (0.1 as starting point)
- Fort/artillery multiplier percentages (1.2x / 1.5x defaults)

## Existing code bugs to fix when we start implementing
1. `new (bool)(no_players)` → `new bool[no_players]` (placement-new vs array alloc)
2. `bool& is_alive_ptr` can't be reassigned after construction
3. Node coordinates are `int`, need `float`/`double` for R^2
4. Speed function: clamp logic was actually correct (`min` cap), but name and return semantics misleading — it's per-tick displacement, not speed
5. `troops[player.idx][i]` — capital placement assumes player i starts at node i
6. `_update_travelling_troops` has broken for-loop (incomplete code)

## Deferred
- AI attention mechanism — documented in CLAUDE.md, not implementing now
- UI implementation — design captured, building engine first
- Routing refinement for non-convex graphs — dot-product heuristic is fine for convex

## Next
- Design implementation plan for the engine
- Start with Graph class (Poisson generation, node culling)
- Then RiskyGraph with game state and tick logic
