# Knapsack v2: Combinatorial War Solver

## Problem formulation

Maximize `sum(score_j * capture_j)` over enemy frontier nodes `j`, where:

- `capture_j = 1` iff `sum_i(s_ij) > troops_j` (must overwhelm)
- `s_ij >= 0` = troops sent from our node `i` to enemy node `j`
- `sum_j(s_ij) <= troops_i` (per-node budget, not global)
- `s_ij = 0` unless edge `(i, j)` exists

Key difference from v1: budget is per-node. Spending node i's troops on target j reduces what's available for target k. You only attack if you can actually win.

## Greedy solver

1. Sort enemy frontier nodes by `score_j / (degree_j / K + 1)` descending
   - High value nodes preferred
   - Low-degree nodes get priority (they'll get starved if we solve greedily elsewhere first)
   - K is a tunable constant
2. For each enemy node j in order:
   - Look at neighbors of j that we own
   - Sum their available (remaining) troops
   - If sum > troops_j: attack. Allocate troops from our nodes, draining smallest-degree first
   - If not: skip
3. Draining smallest-degree first: a degree-1 node can only help one target, so use it before the degree-5 node that could supply several

## Implementation

Use `direct_commands_out` (troop send commands) rather than attention deltas. The solver knows exactly which edges to send troops on and how many — no need to go through the attention gradient flow.

## Test plan

Build test cases of increasing complexity with production disabled:
1. Two nodes: 1 ours, 1 enemy. Can we overwhelm? Do we attack? Do we refuse if we can't win?
2. Star: 1 enemy center, multiple of our leaves. Can we concentrate?
3. K_{n,m}: multiple of ours, multiple enemies. Does greedy allocation work?
4. Mixed: some targets capturable, some not. Does it correctly skip losing fights?
