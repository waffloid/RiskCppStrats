#include "ai/sub_agents/expansion_agent.hpp"
#include "engine/game.hpp"

void ExpansionSubAgent::score(const Game& game, int player_id,
                               std::vector<float>& scores_out,
                               PlayerCommands& /*direct_commands_out*/) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];

        if (nd.state == NodeState::CAPITAL) continue;
        if (nd.owner == player_id) continue;

        // Non-owned node adjacent to our territory.
        // Score by number of owned neighbors: nodes surrounded by more of our
        // territory are easier to capture and fill gaps before pushing frontier.
        int owned_nbrs = 0;
        for (int nbr : graph.neighbors(node)) {
            if (nodes_data[nbr].owner == player_id) owned_nbrs++;
        }
        if (owned_nbrs > 0) {
            scores_out[node] = config_.expansion_border_score * owned_nbrs;
        }
    }
}
