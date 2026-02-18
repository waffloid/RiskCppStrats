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
    add_sub_agent(std::make_unique<EconomySubAgent>(config_), config_.economy_pool_weight);
    add_sub_agent(std::make_unique<ExpansionSubAgent>(config_), config_.expansion_pool_weight);
}

DistributionAIPlayer::DistributionAIPlayer(int player_id, NoDefaults)
    : player_id_(player_id) {}

DistributionAIPlayer::DistributionAIPlayer(int player_id, const ModelConfig& cfg)
    : player_id_(player_id), config_(cfg) {}

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
        decision_snapshot_ = AIDecisionSnapshot{};
    }

    // Linear-combine-then-softmax pipeline:
    // 1. Each sub-agent produces raw scores
    // 2. Weighted sum: combined[i] = Σ_k(weight_k * scores_k[i])
    // 3. Single softmax(combined, global_beta) → distribution
    std::vector<float> combined(n, 0.0f);
    PlayerCommands direct_cmds;

    for (auto& slot : sub_agents_) {
        std::fill(scratch_scores_.begin(), scratch_scores_.end(), 0.0f);

        if (metrics_enabled_) {
            slot.agent->metrics_out = &metrics_;
        }

        slot.agent->score(game, player_id, scratch_scores_, direct_cmds);

        // Max-normalize: scale each agent's scores to [0,1] so pool weights
        // control relative importance between agents, not raw score magnitudes.
        float max_score = 0.0f;
        for (int i = 0; i < n; i++) {
            if (scratch_scores_[i] > max_score) max_score = scratch_scores_[i];
        }
        if (max_score > 0.0f) {
            float inv_max = 1.0f / max_score;
            for (int i = 0; i < n; i++) {
                scratch_scores_[i] *= inv_max;
            }
        }

        // Accumulate weighted normalized scores into combined vector
        for (int i = 0; i < n; i++) {
            combined[i] += slot.weight * scratch_scores_[i];
        }

        if (metrics_enabled_) {
            SubAgentSnapshot snap;
            snap.name = slot.agent->name();
            snap.weight = slot.weight;
            snap.raw_scores = scratch_scores_;
            decision_snapshot_.sub_agents.push_back(std::move(snap));
        }
    }

    // Suppress unscored nodes: set combined=0 to large negative so they
    // contribute ~0 to softmax. Otherwise 300+ zero-scored nodes each add
    // exp(0) and dilute the scored targets.
    for (int i = 0; i < n; i++) {
        if (combined[i] == 0.0f) {
            combined[i] = -1e6f;
        }
    }

    // Single softmax on combined scores → distribution (no smoothing)
    cur_distribution_ = softmax(combined, config_.global_beta);

    // Compute total owned troops for potential
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

    // Compute deficit, then solve Poisson to get smooth potential field
    std::vector<float> deficit = potential_solver_(game.graph(), current_troops, cur_distribution_, total_troops);
    solve_graph_poisson(game.graph(), deficit, warm_phi_);
    std::vector<float>& potential = warm_phi_;

    // Capture pipeline-level snapshot for viz
    if (metrics_enabled_) {
        decision_snapshot_.combined_scores = combined;
        decision_snapshot_.smoothed = cur_distribution_.weights;
        decision_snapshot_.gradient = potential;
        decision_snapshot_.current_troops = current_troops;
        decision_snapshot_.total_owned_troops = total_troops;
    }

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

    // Transport: use greedy multi-neighbor solver (same as gym)
    auto transport_cmds = transport_solver_greedy(
        game.graph(), game.node_data(), potential, player_id,
        scratch_masked_, config_.transport_outflow_rate, config_.transport_min_troops);
    for (auto& cmd : transport_cmds) {
        out.troops.push_back(cmd);
    }

    // Compute distribution metrics
    if (metrics_enabled_ && n > 0) {
        float entropy = 0.0f;
        for (int i = 0; i < n; i++) {
            float w = cur_distribution_.weights[i];
            if (w > 1e-8f) {
                entropy -= w * std::log(w);
            }
        }
        metrics_.distribution_entropy = entropy;

        float hhi = 0.0f;
        for (int i = 0; i < n; i++) {
            float w = cur_distribution_.weights[i];
            hhi += w * w;
        }
        metrics_.distribution_concentration = hhi;

        int positive_nodes = 0;
        int flowing_in = 0;
        for (int i = 0; i < n; i++) {
            if (potential[i] > 0.0f) {
                positive_nodes++;
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
