#include "ai/sub_agents/economy_agent.hpp"
#include "engine/game.hpp"
#include "systems/economy/economy_solvers.hpp"

void EconomySubAgent::score(const Game& game, int player_id,
                             std::vector<float>& scores_out,
                             PlayerCommands& direct_commands_out) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        if (nd.owner != player_id) continue;
        if (nd.state == NodeState::CAPITAL) continue;

        const auto& nbrs = graph.neighbors(node);

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

        if (nd.state == NodeState::DEFAULT || nd.state == NodeState::FACTORY) {
            scores_out[node] = static_cast<float>(balance) * FACTORY_POWERPLANT_SCORE;
            if (nd.state == NodeState::DEFAULT) {
                scores_out[node] += UNBUILT_NODE_SCORE;
            }
        }
    }

    // Build commands via extracted solver
    BuildPlan builds = economy_solver_greedy(graph, nodes_data, player_id, game.config());
    for (const auto& cmd : builds.steps) {
        direct_commands_out.builds.push_back(cmd);
    }
}
