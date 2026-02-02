#include "combat.hpp"
#include <algorithm>

void resolve_combat(NodeData& node_data, int node_idx,
                    const Graph& graph, const std::vector<NodeData>& all_nodes,
                    int n_players, const GameConfig& config) {
    // Count how many players have troops here
    int active_players = 0;
    for (int p = 0; p < n_players; p++) {
        if (node_data.troops[p] > 0) active_players++;
    }
    if (active_players < 2) return;

    // Compute base attack and defense vectors
    std::vector<int> attack(n_players, 0);
    std::vector<int> defense(n_players, 0);

    for (int p = 0; p < n_players; p++) {
        attack[p] = node_data.troops[p] / config.attack_divisor;
        defense[p] = node_data.troops[p] / config.defense_divisor;
    }

    // Fort: multiplies the node owner's defense
    if (node_data.state == NodeState::FORT && node_data.owner >= 0) {
        int owner = node_data.owner;
        defense[owner] = static_cast<int>(
            static_cast<float>(defense[owner]) * config.fort_defense_mult);
    }

    // Artillery at neighboring nodes: multiplies the artillery owner's attack HERE
    const auto& node = graph.nodes[node_idx];
    for (int nbr_idx : node.neighbor_indices) {
        const auto& nbr_data = all_nodes[nbr_idx];
        if (nbr_data.state == NodeState::ARTILLERY && nbr_data.owner >= 0) {
            int owner = nbr_data.owner;
            attack[owner] = static_cast<int>(
                static_cast<float>(attack[owner]) * config.artillery_attack_mult);
        }
    }

    // Compute losses: loss_i = max(sum_{j!=i} attack_j - defense_i, 0)
    for (int p = 0; p < n_players; p++) {
        if (node_data.troops[p] == 0) continue;

        int total_enemy_attack = 0;
        for (int j = 0; j < n_players; j++) {
            if (j != p) total_enemy_attack += attack[j];
        }

        int loss = std::max(total_enemy_attack - defense[p], 0);
        node_data.troops[p] = std::max(node_data.troops[p] - loss, 0);
    }
}
