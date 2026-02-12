#include "ai/sub_agents/economy_agent.hpp"
#include "engine/game.hpp"

void EconomySubAgent::score(const Game& game, int player_id,
                             std::vector<float>& scores_out,
                             PlayerCommands& direct_commands_out) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();
    const auto& cfg = game.config();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        if (nd.owner != player_id) continue;
        if (nd.state == NodeState::CAPITAL) continue;

        if (nd.state != NodeState::DEFAULT) continue;  // only score unbuilt nodes

        // Count adjacent factories/capitals owned by us
        int producer_neighbors = 0;
        for (int nbr : graph.neighbors(node)) {
            const auto& nbd = nodes_data[nbr];
            if (nbd.owner == player_id &&
                (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL))
                producer_neighbors++;
        }

        if (producer_neighbors >= config_.economy_min_pp_neighbors) {
            // Powerplant candidate: surrounded by factories -> high demand
            scores_out[node] = config_.economy_powerplant_score;
        } else {
            // Factory candidate: base demand
            scores_out[node] = config_.economy_factory_score;
        }

        // Parasitic building: if enough troops, build immediately
        int troops = nd.troops[player_id];
        if (producer_neighbors >= config_.economy_min_pp_neighbors
            && troops > cfg.cost_powerplant) {
            direct_commands_out.builds.push_back({node, NodeState::POWERPLANT});
        } else if (troops > cfg.cost_factory) {
            direct_commands_out.builds.push_back({node, NodeState::FACTORY});
        }
    }
}
