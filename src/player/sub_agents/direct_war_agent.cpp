#include "player/sub_agents/direct_war_agent.hpp"
#include "engine/game.hpp"
#include "observability/ai_metrics.hpp"
#include <unordered_set>

void DirectWarSubAgent::contribute(const Game& game, int player_id,
                                    const std::vector<float>& /*current_attention*/,
                                    std::vector<float>& deltas_out,
                                    PlayerCommands& direct_commands_out) {
    auto ctx = build_war_context(game, player_id);
    if (ctx.targets.empty()) return;

    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    const auto& edge_lanes = game.edge_lanes();

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

    // Metrics: v2 efficiency
    if (metrics_out) {
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

    // Build committed attack set: nodes with new commands OR in-flight troops.
    // Without the in-flight check, the retreat logic would recall troops on
    // the tick after v2_solve_attacks decides "enough already in-flight."
    std::unordered_set<int> attack_targets;
    for (const auto& cmd : commands) {
        attack_targets.insert(cmd.to_node);
    }
    for (const auto& t : ctx.targets) {
        for (int nbr : t.our_neighbors) {
            int eidx = graph.edge_between(nbr, t.node_idx);
            if (eidx < 0) continue;
            const auto& el = edge_lanes[eidx];
            int lane_idx = (nbr == el.node_a) ? 0 : 1;
            for (const auto& grp : el.lanes[lane_idx].groups) {
                if (grp.owner == player_id && !grp.retreating) {
                    attack_targets.insert(t.node_idx);
                    goto next_target;  // found in-flight, done with this target
                }
            }
        }
        next_target:;
    }

    // Retreat troops heading toward real enemy nodes we're not committed to
    for (int our_node : ctx.our_nodes) {
        for (int nbr : graph.nodes[our_node].neighbor_indices) {
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
