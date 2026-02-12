#include "ai/sub_agents/knapsack_war_agent.hpp"
#include "engine/game.hpp"
#include "observability/ai_metrics.hpp"
#include <unordered_set>

void KnapsackWarSubAgent::contribute(const Game& game, int player_id,
                                      const std::vector<float>& /*current_attention*/,
                                      std::vector<float>& deltas_out,
                                      PlayerCommands& direct_commands_out) {
    auto ctx = build_war_context(game, player_id);
    if (ctx.targets.empty()) return;

    // Front-line attention: boost our nodes facing real enemies
    for (const auto& t : ctx.targets) {
        for (int nbr : t.our_neighbors) {
            deltas_out[nbr] += FRONT_LINE_ATTENTION * t.value;
        }
    }

    // Solve and emit direct commands
    auto commands = v2_solve_attacks(game, player_id, ctx.targets, ctx.available);
    for (auto& cmd : commands) {
        direct_commands_out.troops.push_back(cmd);
    }

    // Metrics: knapsack approximation ratio + v2 efficiency
    if (metrics_out) {
        // Knapsack ratio: run greedy vs optimal on the frontier
        float budget = 0.0f;
        auto frontier = extract_frontier(game, player_id, budget);
        if (!frontier.empty()) {
            auto greedy_sel  = knapsack_greedy(frontier, budget);
            auto optimal_sel = knapsack_optimal(frontier, budget);
            float gv = selection_value(frontier, greedy_sel);
            float ov = selection_value(frontier, optimal_sel);
            metrics_out->knapsack_greedy_value  = gv;
            metrics_out->knapsack_optimal_value = ov;
            metrics_out->knapsack_ratio = (ov > 0.0f) ? gv / ov : 1.0f;
            metrics_out->knapsack_n_targets = static_cast<int>(frontier.size());
            metrics_out->knapsack_budget = budget;
        }

        // V2 efficiency: value of distinct targets attacked / troops committed
        std::unordered_set<int> attacked_nodes;
        float troops_sum = 0.0f;
        for (const auto& cmd : commands) {
            attacked_nodes.insert(cmd.to_node);
            troops_sum += static_cast<float>(cmd.count);
        }
        float value_sum = 0.0f;
        for (const auto& t : ctx.targets) {
            if (attacked_nodes.count(t.node_idx)) value_sum += t.value;
        }
        metrics_out->v2_value_captured = value_sum;
        metrics_out->v2_troops_spent = troops_sum;
        metrics_out->v2_efficiency = (troops_sum > 0.0f) ? value_sum / troops_sum : 0.0f;
        metrics_out->v2_targets_attacked = static_cast<int>(attacked_nodes.size());
    }

    // Build attack set from commands only (no in-flight tracking)
    std::unordered_set<int> attack_targets;
    for (const auto& cmd : commands) {
        attack_targets.insert(cmd.to_node);
    }

    // Retreat troops heading toward real enemy nodes we're not attacking
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    const auto& edge_lanes = game.edge_lanes();
    for (int our_node : ctx.our_nodes) {
        for (int nbr : graph.neighbors(our_node)) {
            int nbr_owner = nodes_data[nbr].owner;
            if (nbr_owner < 0 || nbr_owner == player_id) continue;
            if (nbr_owner >= game.n_real_players()) continue;
            if (attack_targets.count(nbr)) continue;

            int eidx = graph.edge_between(our_node, nbr);
            if (eidx < 0) continue;
            const auto& el = edge_lanes[eidx];
            int lane_idx = (our_node == el.node_a) ? 0 : 1;
            for (const auto& g : el.lanes[lane_idx].groups) {
                if (g.owner == player_id && !g.retreating) {
                    direct_commands_out.retreats.push_back({our_node});
                    break;
                }
            }
        }
    }
}
