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
    add_sub_agent(std::make_unique<EconomySubAgent>(config_), 1.0f);
    add_sub_agent(std::make_unique<ExpansionSubAgent>(config_), 1.0f);
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
        TroopDistribution dist = softmax(scratch_scores_, slot.agent->beta());

        // Capture sub-agent snapshot for viz
        if (metrics_enabled_) {
            SubAgentSnapshot snap;
            snap.name = slot.agent->name();
            snap.weight = slot.weight;
            snap.beta = slot.agent->beta();
            snap.raw_scores = scratch_scores_;
            snap.distribution = dist.weights;
            decision_snapshot_.sub_agents.push_back(std::move(snap));
        }

        agent_dists.push_back(std::move(dist));
        agent_weights.push_back(slot.weight);
    }

    // Pool all agent distributions
    std::vector<const TroopDistribution*> dist_ptrs;
    for (const auto& d : agent_dists) dist_ptrs.push_back(&d);
    TroopDistribution pooled = pool(dist_ptrs, agent_weights);

    // EMA smooth against previous tick's distribution
    TroopDistribution smoothed = ema(pooled, prev_distribution_, config_.ema_alpha);
    prev_distribution_ = smoothed;

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

    // Compute potential (deficit signal for transport)
    std::vector<float> potential = potential_solver_(game.graph(), current_troops, smoothed, total_troops);

    // Capture pipeline-level snapshot for viz
    if (metrics_enabled_) {
        decision_snapshot_.pooled = pooled.weights;
        decision_snapshot_.smoothed = smoothed.weights;
        decision_snapshot_.gradient = potential;  // viz uses "gradient" field name
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

    // Execute transport based on potential (skip masked nodes)
    execute_transport(game, out, potential, scratch_masked_);

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

        // Potential utilization: fraction of positive-potential nodes that have troops flowing in
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

void DistributionAIPlayer::execute_transport(const Game& game, PlayerCommands& out,
                                              const std::vector<float>& potential,
                                              const std::vector<bool>& masked) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    int n = graph.num_nodes();

    int min_send = config_.transport_min_send;

    // For each owned node with surplus (negative potential),
    // send troops toward neighboring nodes with deficit (positive potential).
    for (int i = 0; i < n; i++) {
        if (masked[i]) continue;
        if (nodes_data[i].owner != player_id_) continue;

        float excess = -potential[i];  // positive means we have MORE than desired
        if (excess < static_cast<float>(min_send)) continue;

        int available = nodes_data[i].troops[player_id_] - 1;  // keep 1 garrison
        if (available < min_send) continue;

        // Find best neighbor with positive potential (deficit — wants troops)
        int best_nbr = -1;
        float best_potential = 0.0f;
        for (int nbr : graph.neighbors(i)) {
            if (potential[nbr] > best_potential) {
                best_potential = potential[nbr];
                best_nbr = nbr;
            }
        }

        if (best_nbr >= 0) {
            int send = std::min(available, static_cast<int>(excess));
            send = std::min(send, static_cast<int>(best_potential + 0.5f));
            send = std::max(send, min_send);
            send = std::min(send, available);
            if (send >= min_send) {
                out.troops.push_back({i, best_nbr, send});
            }
        }
    }
}
