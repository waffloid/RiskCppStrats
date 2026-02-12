#include "player/sub_agents/bootstrap_economy_agent.hpp"
#include "engine/game.hpp"
#include "systems/economy/economy_solvers.hpp"

void BootstrapEconomySubAgent::generate_plan(const Game& game, int player_id) {
    // Delegate plan generation to extracted solver.
    BuildPlan result = economy_solver_bootstrap(
        game.graph(), game.node_data(), player_id, game.config(), NUM_FACTORIES);

    for (const auto& cmd : result.steps) {
        plan_.push_back({cmd.node_idx, cmd.structure});
    }
}

void BootstrapEconomySubAgent::contribute(const Game& game, int player_id,
                                           const std::vector<float>& current_attention,
                                           std::vector<float>& deltas_out,
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
        // Bootstrap phase: focus all attention on the current target,
        // suppress attention elsewhere to prevent troop scatter
        const BuildStep& step = plan_[plan_cursor_];
        deltas_out[step.node] += BOOTSTRAP_ATTENTION;

        const auto& graph = game.graph();
        for (int i = 0; i < graph.num_nodes(); i++) {
            if (i != step.node && nodes_data[i].owner == player_id) {
                deltas_out[i] -= BOOTSTRAP_DAMPEN;
            }
        }

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
        fallback_.contribute(game, player_id, current_attention, deltas_out, direct_commands_out);
    }
}
