#include "player/expansion_agent.hpp"
#include "engine/game.hpp"

void ExpansionSubAgent::contribute(const Game& game, int player_id,
                                    const std::vector<float>& /*current_attention*/,
                                    std::vector<float>& deltas_out,
                                    PlayerCommands& /*direct_commands_out*/) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];

        // Skip capitals (handled by economy) and our own nodes
        if (nd.state == NodeState::CAPITAL) continue;
        if (nd.owner == player_id) continue;

        // Non-owned node (unowned, neutral, or enemy) adjacent to our territory
        bool adjacent_to_us = false;
        const auto& nbrs = graph.nodes[node].neighbor_indices;
        for (int nbr : nbrs) {
            if (nodes_data[nbr].owner == player_id) {
                adjacent_to_us = true;
                break;
            }
        }
        if (adjacent_to_us) {
            deltas_out[node] += BORDER_ATTENTION;
        }
    }
}
