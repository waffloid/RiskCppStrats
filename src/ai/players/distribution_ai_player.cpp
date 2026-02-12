#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

DistributionAIPlayer::DistributionAIPlayer(int player_id)
    : player_id_(player_id) {
    // Default sub-agents: economy + expansion (same as old AttentionAI defaults)
    add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
    add_sub_agent(std::make_unique<ExpansionSubAgent>(), 1.0f);
}

DistributionAIPlayer::DistributionAIPlayer(int player_id, NoDefaults)
    : player_id_(player_id) {}

void DistributionAIPlayer::add_sub_agent(std::unique_ptr<DistributionSubAgent> agent,
                                          float weight) {
    sub_agents_.push_back({std::move(agent), weight});
}

void DistributionAIPlayer::decide(const Game& game, int player_id, PlayerCommands& out) {
    int n = game.graph().num_nodes();

    // Resize scratch buffers
    scratch_scores_.assign(n, 0.0f);
    scratch_masked_.assign(n, false);

    if (metrics_enabled_) {
        metrics_ = AIMetricsSnapshot{};
    }

    // Collect per-agent distributions and direct commands
    std::vector<TroopDistribution> agent_dists;
    std::vector<float> agent_weights;
    PlayerCommands direct_cmds;

    for (auto& slot : sub_agents_) {
        // Reset scores
        std::fill(scratch_scores_.begin(), scratch_scores_.end(), 0.0f);

        // Set metrics pointer
        if (metrics_enabled_) {
            slot.agent->metrics_out = &metrics_;
        }

        // Get raw scores + direct commands
        slot.agent->score(game, player_id, scratch_scores_, direct_cmds);

        // Convert scores → distribution via softmax
        agent_dists.push_back(softmax(scratch_scores_, slot.agent->beta()));
        agent_weights.push_back(slot.weight);
    }

    // Pool all agent distributions
    std::vector<const TroopDistribution*> dist_ptrs;
    for (const auto& d : agent_dists) dist_ptrs.push_back(&d);
    TroopDistribution pooled = pool(dist_ptrs, agent_weights);

    // EMA smooth against previous tick's distribution
    TroopDistribution smoothed = ema(pooled, prev_distribution_, ema_alpha);
    prev_distribution_ = smoothed;

    // Compute total owned troops for gradient
    int total_troops = 0;
    const auto& nodes_data = game.node_data();
    for (int i = 0; i < n; i++) {
        if (nodes_data[i].owner == player_id) {
            total_troops += nodes_data[i].troops[player_id];
        }
    }

    // Get current troop counts per node
    std::vector<int> current_troops(n, 0);
    for (int i = 0; i < n; i++) {
        current_troops[i] = nodes_data[i].troops[player_id];
    }

    // Compute deficit gradient
    std::vector<float> gradient = distribution_to_gradient(smoothed, current_troops, total_troops);

    // Merge direct commands into output
    out.builds.insert(out.builds.end(), direct_cmds.builds.begin(), direct_cmds.builds.end());
    out.retreats.insert(out.retreats.end(), direct_cmds.retreats.begin(), direct_cmds.retreats.end());

    // Build mask: nodes used by direct troop commands shouldn't be overridden
    std::unordered_set<int> direct_sources;
    for (const auto& cmd : direct_cmds.troops) {
        direct_sources.insert(cmd.from_node);
        out.troops.push_back(cmd);
    }
    for (int i = 0; i < n; i++) {
        scratch_masked_[i] = direct_sources.count(i) > 0;
    }

    // Execute transport based on gradient (skip masked nodes)
    execute_transport(game, out, gradient, scratch_masked_);

    // Compute distribution metrics
    if (metrics_enabled_ && n > 0) {
        // Distribution entropy
        float entropy = 0.0f;
        for (int i = 0; i < n; i++) {
            float w = smoothed.weights[i];
            if (w > 1e-8f) {
                entropy -= w * std::log(w);
            }
        }
        metrics_.distribution_entropy = entropy;

        // Herfindahl concentration index (sum of squared weights, 1/n = uniform, 1.0 = single node)
        float hhi = 0.0f;
        for (int i = 0; i < n; i++) {
            float w = smoothed.weights[i];
            hhi += w * w;
        }
        metrics_.distribution_concentration = hhi;

        // Gradient utilization: fraction of positive-gradient nodes that have troops flowing in
        int positive_nodes = 0;
        int flowing_in = 0;
        for (int i = 0; i < n; i++) {
            if (gradient[i] > 0.0f) {
                positive_nodes++;
                // Check if any troop command targets this node
                for (const auto& cmd : out.troops) {
                    if (cmd.to_node == i) { flowing_in++; break; }
                }
            }
        }
        metrics_.distribution_gradient_util = (positive_nodes > 0)
            ? static_cast<float>(flowing_in) / static_cast<float>(positive_nodes)
            : 0.0f;
    }
}

void DistributionAIPlayer::execute_transport(const Game& game, PlayerCommands& out,
                                              const std::vector<float>& gradient,
                                              const std::vector<bool>& masked) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    int n = graph.num_nodes();

    // For each owned node with positive available troops and negative gradient
    // (i.e. excess troops), send troops toward neighboring nodes with positive gradient.
    for (int i = 0; i < n; i++) {
        if (masked[i]) continue;
        if (nodes_data[i].owner != player_id_) continue;

        float excess = -gradient[i];  // positive means we have MORE than desired
        if (excess < static_cast<float>(MIN_TROOPS_TO_SEND)) continue;

        int available = nodes_data[i].troops[player_id_] - 1;  // keep 1 garrison
        if (available < MIN_TROOPS_TO_SEND) continue;

        // Find best neighbor with positive gradient (wants troops)
        int best_nbr = -1;
        float best_gradient = 0.0f;
        for (int nbr : graph.neighbors(i)) {
            if (gradient[nbr] > best_gradient) {
                best_gradient = gradient[nbr];
                best_nbr = nbr;
            }
        }

        if (best_nbr >= 0) {
            int send = std::min(available, static_cast<int>(excess));
            send = std::min(send, static_cast<int>(best_gradient + 0.5f));
            send = std::max(send, MIN_TROOPS_TO_SEND);
            send = std::min(send, available);
            if (send >= MIN_TROOPS_TO_SEND) {
                out.troops.push_back({i, best_nbr, send});
            }
        }
    }
}
