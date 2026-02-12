#include "ai/sub_agents/bootstrap_economy_agent.hpp"
#include "engine/game.hpp"
#include "systems/economy/economy_solvers.hpp"

void BootstrapEconomySubAgent::generate_plan(const Game& game, int player_id) {
    BuildPlan result = economy_solver_bootstrap(
        game.graph(), game.node_data(), player_id, game.config(), NUM_FACTORIES);

    for (const auto& cmd : result.steps) {
        plan_.push_back({cmd.node_idx, cmd.structure});
    }
}

void BootstrapEconomySubAgent::score(const Game& game, int player_id,
                                      std::vector<float>& scores_out,
                                      PlayerCommands& direct_commands_out) {
    if (!planned_) {
        generate_plan(game, player_id);
        planned_ = true;
    }

    const auto& nodes_data = game.node_data();
    const auto& config = game.config();

    // Advance cursor past completed steps
    while (plan_cursor_ < static_cast<int>(plan_.size())) {
        const BuildStep& step = plan_[plan_cursor_];
        if (nodes_data[step.node].state == step.structure) {
            plan_cursor_++;
        } else {
            break;
        }
    }

    if (plan_cursor_ < static_cast<int>(plan_.size())) {
        // Bootstrap phase: concentrate all score on the current target.
        // High β (5.0) ensures softmax peaks sharply — no need for negative dampening.
        bootstrapping_ = true;
        const BuildStep& step = plan_[plan_cursor_];
        scores_out[step.node] = BOOTSTRAP_SCORE;

        // Build if we own the node and have enough troops
        if (nodes_data[step.node].owner == player_id) {
            int troops = nodes_data[step.node].troops[player_id];
            int cost = (step.structure == NodeState::FACTORY)
                       ? config.cost_factory
                       : config.cost_powerplant;
            if (troops > cost) {
                direct_commands_out.builds.push_back({step.node, step.structure});
            }
        }
    } else {
        // Plan complete — delegate to standard economy logic
        bootstrapping_ = false;
        fallback_.score(game, player_id, scores_out, direct_commands_out);
    }
}
