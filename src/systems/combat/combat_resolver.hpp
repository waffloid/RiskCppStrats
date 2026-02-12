#ifndef CRISKY_COMBAT_RESOLVER_HPP
#define CRISKY_COMBAT_RESOLVER_HPP

#include <vector>
#include "engine/game_config.hpp"
#include "engine/game_state.hpp"
#include "engine/graph.hpp"

// Combat state: fractional damage accumulation carried between ticks.
// Separated from NodeData so combat can be resolved as a pure function.
struct CombatState {
    std::vector<std::vector<float>> accumulated_damage; // [node][player]

    void init(int n_nodes, int n_players) {
        accumulated_damage.assign(n_nodes, std::vector<float>(n_players, 0.0f));
    }

    void clear_node(int node, int n_players) {
        accumulated_damage[node].assign(n_players, 0.0f);
    }
};

// Result of combat resolution at a single node.
struct NodeCombatResult {
    std::vector<int> casualties;       // [player] — troops killed this tick
    std::vector<float> updated_damage; // [player] — updated damage accumulator
};

// Pure function: compute casualties at a single node.
// Does NOT mutate anything — caller applies results.
NodeCombatResult compute_node_casualties(
    const NodeData& node,
    int node_idx,
    const Graph& graph,
    const std::vector<NodeData>& all_nodes,
    const std::vector<float>& accumulated_damage,
    int n_players,
    const GameConfig& config,
    float dt);

// Aggregate result of combat across all nodes.
struct AllCombatResults {
    std::vector<NodeCombatResult> per_node; // [node]
    CombatState updated_state;
    std::vector<int> total_deaths; // [player]
};

// Pure function: resolve combat at all nodes simultaneously.
// Returns updated state and per-player death totals.
AllCombatResults resolve_all_node_combat(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    const CombatState& prev_state,
    int n_players,
    const GameConfig& config,
    float dt);

// Apply combat results to node data (the mutation step).
// Subtracts casualties from troops, clears damage for dead players.
void apply_combat_results(
    std::vector<NodeData>& nodes,
    const std::vector<NodeCombatResult>& per_node_results,
    CombatState& state,
    int n_players);

#endif
