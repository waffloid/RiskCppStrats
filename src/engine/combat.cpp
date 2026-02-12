#include "combat.hpp"
#include <algorithm>

void resolve_combat(NodeData& node_data, int node_idx,
                    const Graph& graph, const std::vector<NodeData>& all_nodes,
                    int n_players, const GameConfig& config, float dt) {
    // Count how many players have troops here
    int active_players = 0;
    for (int p = 0; p < n_players; p++) {
        if (node_data.troops[p] > 0) active_players++;
    }
    if (active_players < 2) {
        // No combat — clear accumulated damage to avoid stale buildup
        for (int p = 0; p < n_players; p++) {
            node_data.accumulated_damage[p] = 0.0f;
        }
        return;
    }

    // Compute base attack and defense vectors (float math throughout)
    std::vector<float> attack(n_players, 0.0f);
    std::vector<float> defense(n_players, 0.0f);

    for (int p = 0; p < n_players; p++) {
        attack[p] = static_cast<float>(node_data.troops[p]) / config.attack_divisor;
        defense[p] = static_cast<float>(node_data.troops[p]) / config.defense_divisor;
    }

    // Fort: multiplies the node owner's defense
    if (node_data.state == NodeState::FORT && node_data.owner >= 0) {
        int owner = node_data.owner;
        defense[owner] *= config.fort_defense_mult;
    }

    // Artillery at neighboring nodes: multiplies the artillery owner's attack HERE
    const auto& node = graph.nodes[node_idx];
    for (int nbr_idx : graph.neighbors(node_idx)) {
        const auto& nbr_data = all_nodes[nbr_idx];
        if (nbr_data.state == NodeState::ARTILLERY && nbr_data.owner >= 0) {
            int owner = nbr_data.owner;
            attack[owner] *= config.artillery_attack_mult;
        }
    }

    // Compute losses: loss_i = max(sum_{j!=i} attack_j - defense_i, 0)
    float dt_scale = dt / config.base_dt;
    for (int p = 0; p < n_players; p++) {
        if (node_data.troops[p] == 0) continue;

        float total_enemy_attack = 0.0f;
        for (int j = 0; j < n_players; j++) {
            if (j != p) total_enemy_attack += attack[j];
        }

        float loss = std::max(total_enemy_attack - defense[p], 0.0f);
        float scaled_loss = loss * dt_scale;
        node_data.accumulated_damage[p] += scaled_loss;
        int int_damage = static_cast<int>(node_data.accumulated_damage[p]);
        if (int_damage > 0) {
            node_data.troops[p] = std::max(node_data.troops[p] - int_damage, 0);
            node_data.accumulated_damage[p] -= static_cast<float>(int_damage);
        }
        // Clear accumulator if player has no troops left
        if (node_data.troops[p] == 0) {
            node_data.accumulated_damage[p] = 0.0f;
        }
    }
}
