# Workflow

## Session tracking

Each working session gets a folder in `claude/sessions/` named `NNNN-<short-description>.md`. Detail level is adaptive: bullet points for routine progress, narrative when we hit interesting decisions or debugging. Contains session note, as well as other relevant context which we may wish to preserve.

### Session note template
```
# Session NNNN: <title>
Date: YYYY-MM-DD

## Done
- ...

## Decisions
(table format when non-trivial)
| Decision | Verdict | Reason |
|----------|---------|--------|

## Deferred
(items explicitly chosen NOT to do now, with reasoning -- distinct from "Next")

## Next
- ...
```

### Conventions (adopted from claude-noto)
- **Guiding invariants**: `[MULL]` -- not yet decided. Need concrete design tensions from implementation before choosing. Should be simple filters that resolve "should I do X?" questions, not just restatements of the project brief.
- **`[MULL]` markers**: when something needs thinking, not doing, mark it explicitly in notes. Prevents premature implementation.
- **Quote preservation**: when Joel says something that captures important reasoning, transcribe it directly in the session note rather than paraphrasing.
- **Critical engagement over sycophancy**: prioritize honest analysis and pushback over agreement. Flag when a proposed approach has problems.

### Start of session
1. Read `CLAUDE.md`
2. Read latest session note in `claude/sessions/`
3. Check `git log --oneline -10` for recent history

### End of session
1. Commit any uncommitted work
2. Write session note

## Git conventions
- Commit at natural checkpoints (feature works, test passes, before risky changes)
- Short imperative commit messages; longer body if the change is non-obvious

---

# CRisky — Game Design Reference

## Overview
Risky Strats is a discrete-time strategy game played on geometric graphs embedded in R^2. Players control troops across nodes, build structures, and fight for territory.

## Geometric Graphs

A geometric graph is a subgraph of the graph induced by a point sequence in R^2, where edges connect points within a distance threshold. The graph carries both combinatorial and positional data.

**Poisson process graphs** (primary target): sample points from a region via Poisson process, induce edges within distance threshold, then **cull nodes** with too many neighbors to avoid dense clusters.

Parameters (all tunable):
- Poisson intensity
- Region size
- Edge distance threshold
- Max neighbor count for culling

## Node State

Enum: `DEFAULT`, `CAPITAL`, `FACTORY`, `POWERPLANT`, `FORT`, `ARTILLERY`

## Troops

- Integer-valued
- Per node: a troop vector in Z^n (one component per player)
- Node "owned" by player i iff troop vector is scalar multiple of e_i

## Troop Generation (per tick)

- Capital: 2 troops for owner
- Factory: 1 troop for owner
- Powerplant: +2 troops to each neighboring factory/capital

## Combat

When a node's troop vector has multiple nonzero components:
- Attack vector = troop_vector / 100
- Defense vector = troop_vector / 1000
- Troops lost by P_i = max(sum_{j!=i} attack_j - defense_i, 0)
- **Fort**: multiplies owner's defense by 1.2x (parameterized)
- **Artillery**: multiplies owner's attack by 1.5x at neighboring nodes (parameterized)
- Integer arithmetic throughout

## Building Costs (in troops)

| Structure  | Cost |
|------------|------|
| Factory    | 500  |
| Powerplant | 2500 |
| Fort       | 400  |
| Artillery  | 4000 |

## Troop Movement

**Displacement formula**: `displacement = min(dt, (0.1 + 1/count) * dt / 0.2)` which simplifies to `min(dt, (0.5 + 5/count) * dt)`. Constants parameterized.
- count < 10: clamped to `dt` (max speed)
- count >= 10: displacement decreases with count, approaching `0.5 * dt`
- Bigger armies are slower

**Aggregation**: troops travelling in the same direction on the same edge within `radius(count) = RADIUS_FACTOR * count + buffer` of each other are merged. Only same-direction groups aggregate; opposing groups do not.

**Edge collisions**: when opposing groups meet on an edge, the larger group forces the smaller to retreat. No troop losses — pure retreat. A `forced_retreat` flag prevents a group from being forced twice in one traversal (avoids infinite bouncing). If forced from both sides, the first forcing wins.

**Routing**: troops have a global destination. At each intermediate node, pick the neighbor whose normalized direction has best dot-product similarity with the global target direction. This chains local hops. Works well for convex graphs; may need refinement for non-convex.

**Retreat** (voluntary or forced): reverses travel. Time to return = time already elapsed. Clears global routing info; troop settles at the local origin node.

### Edge troop data structure

Per edge, two sorted lanes (one per direction). Per-tick update:
1. Advance positions by displacement
2. Aggregate same-direction groups within threshold (sweep)
3. Resolve opposite-direction collisions (frontmost leaders, cascade bounded by forced flag)
4. Arrivals deposited at destination node, trigger next-hop routing

**Monotone stack optimization**: bigger army = slower. A smaller trailing group catches a larger leading group (potential aggregation). A larger trailing group falls further behind (skip permanently until configuration changes). Only check aggregation where trailing group is smaller than leading group.

## Architecture (planned)

### Graph
- Nodes with R^2 positions, neighbor lists
- Subgraph extraction
- Poisson process generation with node culling

### RiskyGraph (extends Graph)
- Node state, troop vectors
- Send troops, retreat, build structure
- Update tick: process factories, combat, travelling troops
- Range operations for UI (select by radius, send/retreat in radius)

### TravellingTroop struct
- from_node, local_to_node, global_to_node
- t_sent, t_arrival, no_troops, owner

### RiskyPlayer
- Input state management for human players
- Move buffers

## AI (deferred)

Attention-mechanism based:
- Each node has an attention scalar
- Troop flow driven by attention gradients: outflow ~ softmax(||nbr_attention^+||) * nbr_attention^+_i / ||nbr_attention^+||
- Attention diffuses via discrete Laplacian on graph, normalized by subtracting mean
- Heuristic generators: unoccupied nodes +10/tick, +const per factory/default neighbor, -const per powerplant neighbor (greedy powerplant placement signal)
- Future: message passing / GNN interface

## UI (planned — RayLib)

- Graph visualization with node colors per owner, troop counts displayed
- Fog of war when playing (see own nodes + neighbors only), full vision when spectating
- Click-drag circle selection for multi-node commands
- Hotkeys (qwer) for sending troops (fixed count or percentage)
- Hover highlighting for target node
- Alt+drag for retreat operations within radius
- Single-click node selection also supported

## RL / Heuristics (planned)

- Game engine must support headless mode for RL training
- Heuristic AI players for baseline opponents
- RL agent interface TBD
