# AI Development Notes

## Sub-Agent Architecture
- AttentionAI orchestrator holds weighted sub-agents, each contributing attention deltas + direct commands
- Default: EconomySubAgent (1.0) + ExpansionSubAgent (1.0)
- Future sub-agents: WarSubAgent (knapsack), DefenseSubAgent, CoordinationSubAgent

## Knapsack Frontier Solver (current work)
- Single-pool greedy knapsack at the frontier
- Budget = total troops across our frontier nodes
- Items = opposing frontier nodes, cost = troops there, value = strategic value
- Fort cost adjustment: multiply enemy troops by fort_defense_mult
- Powerplant value: 2 * (number of adjacent enemy factories/capitals it powers)
- Output: attention deltas on selected targets

### Future refinements — troop estimation with foresight
- For our nodes: exponentially weighted average over shortest path length
  - `estimated_troops(node) = sum over owned nodes v of troops(v) * beta^d(v, node)`
  - beta in [0, 1] is a hyperparameter controlling horizon
  - This estimates reinforcement potential at each frontier node
- For enemy nodes: unknown buffer model
  - Start with constant A (in R+) representing uncertainty about enemy strength
  - Decay via rate alpha (in [0, 1]) as we observe the node over time
  - `estimated_enemy_troops(node) = observed_troops + A * alpha^(ticks_observed)`
  - Fresh enemy nodes have high uncertainty buffer, long-observed ones converge to actual

### Future refinements — defensive knapsack
- When enemy troops are incoming toward our nodes, add those destination nodes to the knapsack
- The "cost" of defending = troops needed to survive the incoming attack
- This means the war agent and defense agent share the same knapsack framework

## Coordinated Attacks (future)
Two modes:
1. Simultaneous arrival: send big group first, time smaller groups to arrive together
   - Requires computing travel time = integral of speed over hops, accounting for cbrt(count) slowdown
   - Send small groups delayed so arrival times align
2. Staging/aggregation: route smaller groups to a rally node, merge, then attack
   - Avoids piecemeal retreat from edge collisions
   - Second mode useful when enemy controls edges between us and target

## Generalized Scalar Fields (future)
- The attention field is one scalar field. Consider generalizing to multiple named fields.
- Each field undergoes its own diffusion + delta injection cycle.
- Sub-agents can read/write specific fields.
- This enables GNN-like multi-channel inference over the graph.
- War agent could suppress economy field in contested zones.
- Economy agent reads an "economic potential" field, war agent reads a "threat" field, etc.

## Attention as Action (future)
- Currently attention drives troop flow via gradient ascent.
- Consider: attention deltas as the AI's "action space" — the knapsack solver produces deltas, not direct troop commands.
- This keeps the system end-to-end differentiable (for RL) while being interpretable.
- The attention field acts as a bottleneck/communication channel between sub-agents and the troop flow executor.
