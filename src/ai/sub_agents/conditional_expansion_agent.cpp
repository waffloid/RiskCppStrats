#include "ai/sub_agents/conditional_expansion_agent.hpp"
#include "engine/game.hpp"

void ConditionalExpansionSubAgent::score(const Game& game, int player_id,
                                          std::vector<float>& scores_out,
                                          PlayerCommands& direct_commands_out) {
    const auto& nodes_data = game.node_data();

    int unconstructed = 0, total_owned = 0;
    for (const auto& nd : nodes_data) {
        if (nd.owner == player_id) {
            total_owned++;
            if (nd.state == NodeState::DEFAULT) unconstructed++;
        }
    }

    float frac = (total_owned > 0) ? static_cast<float>(unconstructed) / total_owned : 0.0f;
    if (frac > config_.expansion_unconstructed_cutoff) {
        return;  // all zeros — expansion contributes nothing this tick
    }

    ExpansionSubAgent::score(game, player_id, scores_out, direct_commands_out);
}
