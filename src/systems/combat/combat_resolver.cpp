#include "systems/combat/combat_resolver.hpp"
#include <algorithm>

NodeCombatResult compute_node_casualties(
    const NodeData& node,
    int node_idx,
    const Graph& graph,
    const std::vector<NodeData>& all_nodes,
    const std::vector<float>& accumulated_damage,
    int n_players,
    const GameConfig& config,
    float dt) {

    NodeCombatResult result;
    result.casualties.assign(n_players, 0);
    result.updated_damage.assign(n_players, 0.0f);

    // Count how many players have troops here
    int active_players = 0;
    for (int p = 0; p < n_players; p++) {
        if (node.troops[p] > 0) active_players++;
    }
    if (active_players < 2) {
        // No combat — clear accumulated damage
        return result;
    }

    // Copy incoming damage state
    for (int p = 0; p < n_players; p++) {
        result.updated_damage[p] = accumulated_damage[p];
    }

    // Compute base attack and defense
    std::vector<float> attack(n_players, 0.0f);
    std::vector<float> defense(n_players, 0.0f);

    for (int p = 0; p < n_players; p++) {
        attack[p] = static_cast<float>(node.troops[p]) / config.attack_divisor;
        defense[p] = static_cast<float>(node.troops[p]) / config.defense_divisor;
    }

    // Fort: multiplies the node owner's defense
    if (node.state == NodeState::FORT && node.owner >= 0) {
        defense[node.owner] *= config.fort_defense_mult;
    }

    // Artillery at neighboring nodes: multiplies the artillery owner's attack HERE
    for (int nbr_idx : graph.neighbors(node_idx)) {
        const auto& nbr_data = all_nodes[nbr_idx];
        if (nbr_data.state == NodeState::ARTILLERY && nbr_data.owner >= 0) {
            attack[nbr_data.owner] *= config.artillery_attack_mult;
        }
    }

    // Compute losses: loss_i = max(sum_{j!=i} attack_j - defense_i, 0)
    float dt_scale = dt / config.base_dt;
    for (int p = 0; p < n_players; p++) {
        if (node.troops[p] == 0) continue;

        float total_enemy_attack = 0.0f;
        for (int j = 0; j < n_players; j++) {
            if (j != p) total_enemy_attack += attack[j];
        }

        float loss = std::max(total_enemy_attack - defense[p], 0.0f);
        float scaled_loss = loss * dt_scale;
        result.updated_damage[p] += scaled_loss;
        int int_damage = static_cast<int>(result.updated_damage[p]);
        if (int_damage > 0) {
            result.casualties[p] = std::min(int_damage, node.troops[p]);
            result.updated_damage[p] -= static_cast<float>(int_damage);
        }
        // Clear accumulator if player will have no troops left
        if (node.troops[p] - result.casualties[p] <= 0) {
            result.updated_damage[p] = 0.0f;
        }
    }

    return result;
}

AllCombatResults resolve_all_node_combat(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    const CombatState& prev_state,
    int n_players,
    const GameConfig& config,
    float dt) {

    AllCombatResults results;
    results.total_deaths.assign(n_players, 0);
    results.updated_state.init(graph.num_nodes(), n_players);
    results.per_node.reserve(graph.num_nodes());

    for (int i = 0; i < graph.num_nodes(); i++) {
        NodeCombatResult nr = compute_node_casualties(
            nodes[i], i, graph, nodes,
            prev_state.accumulated_damage[i],
            n_players, config, dt);

        results.updated_state.accumulated_damage[i] = nr.updated_damage;
        for (int p = 0; p < n_players; p++) {
            results.total_deaths[p] += nr.casualties[p];
        }
        results.per_node.push_back(std::move(nr));
    }

    return results;
}

void apply_combat_results(
    std::vector<NodeData>& nodes,
    const std::vector<NodeCombatResult>& per_node_results,
    CombatState& state,
    int n_players) {

    int n = static_cast<int>(nodes.size());
    for (int i = 0; i < n; i++) {
        for (int p = 0; p < n_players; p++) {
            nodes[i].troops[p] = std::max(nodes[i].troops[p] - per_node_results[i].casualties[p], 0);
        }
        state.accumulated_damage[i] = per_node_results[i].updated_damage;
    }
}
