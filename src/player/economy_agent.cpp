#include "player/economy_agent.hpp"
#include "engine/game.hpp"

void EconomySubAgent::contribute(const Game& game, int player_id,
                                  const std::vector<float>& /*current_attention*/,
                                  std::vector<float>& deltas_out,
                                  PlayerCommands& direct_commands_out) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();
    const int cost_factory = game.config().cost_factory;
    const int cost_powerplant = game.config().cost_powerplant;

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        if (nd.owner != player_id) continue;
        if (nd.state == NodeState::CAPITAL) continue;

        const auto& nbrs = graph.nodes[node].neighbor_indices;

        // Count adjacent factories/capitals and powerplants owned by us
        int factory_neighbors = 0;
        int powerplant_neighbors = 0;
        for (int nbr : nbrs) {
            const auto& nbd = nodes_data[nbr];
            if (nbd.owner == player_id) {
                if (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL)
                    factory_neighbors++;
                if (nbd.state == NodeState::POWERPLANT)
                    powerplant_neighbors++;
            }
        }

        int balance = factory_neighbors - powerplant_neighbors;

        // Attention deltas: attract troops to nodes that need building
        if (nd.state == NodeState::DEFAULT || nd.state == NodeState::FACTORY) {
            deltas_out[node] += static_cast<float>(balance)
                                * FACTORY_POWERPLANT_DELTA_DESIRE;

            if (nd.state == NodeState::DEFAULT) {
                deltas_out[node] += UNBUILT_NODE_ATTENTION;
            }
        }

        // Direct build commands
        int troops = nd.troops[player_id];
        if (troops > cost_powerplant && nd.state != NodeState::POWERPLANT && balance > 0) {
            direct_commands_out.builds.push_back({node, NodeState::POWERPLANT});
        } else if (troops > cost_factory && nd.state != NodeState::FACTORY
                   && nd.state != NodeState::POWERPLANT && balance <= 0) {
            direct_commands_out.builds.push_back({node, NodeState::FACTORY});
        }
    }
}
