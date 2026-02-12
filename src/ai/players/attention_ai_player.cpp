#include "ai/players/attention_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

AttentionAIPlayer::AttentionAIPlayer(int player_id) : player_id_(player_id) {
    // Default sub-agents matching original behavior
    sub_agents_.push_back({std::make_unique<EconomySubAgent>(), 1.0f});
    sub_agents_.push_back({std::make_unique<ExpansionSubAgent>(), 1.0f});
}

AttentionAIPlayer::AttentionAIPlayer(int player_id, NoDefaults) : player_id_(player_id) {
    // No sub-agents — caller adds their own
}

void AttentionAIPlayer::add_sub_agent(std::unique_ptr<AttentionSubAgent> agent, float weight) {
    sub_agents_.push_back({std::move(agent), weight});
}

void AttentionAIPlayer::decide(const Game& game, int player_id, PlayerCommands& out) {
    if (!game.is_alive(player_id)) return;

    int n = game.graph().num_nodes();

    // Lazy-init attention vector
    if (!initialized_) {
        attention_.assign(n, 0.0f);
        initialized_ = true;
    }

    // 1. Collect weighted deltas and direct commands from sub-agents
    scratch_total_deltas_.assign(n, 0.0f);
    scratch_sub_deltas_.resize(n);
    for (SubAgentSlot& slot : sub_agents_) {
        std::fill(scratch_sub_deltas_.begin(), scratch_sub_deltas_.end(), 0.0f);
        PlayerCommands direct;
        slot.agent->metrics_out = metrics_enabled_ ? &metrics_ : nullptr;
        slot.agent->contribute(game, player_id, attention_, scratch_sub_deltas_, direct);

        for (int i = 0; i < n; i++) {
            scratch_total_deltas_[i] += slot.weight * scratch_sub_deltas_[i];
        }

        // Merge direct commands
        out.troops.insert(out.troops.end(), direct.troops.begin(), direct.troops.end());
        out.builds.insert(out.builds.end(), direct.builds.begin(), direct.builds.end());
        out.retreats.insert(out.retreats.end(), direct.retreats.begin(), direct.retreats.end());
    }

    // 2. Diffuse attention (sparse Laplacian, no matrix)
    diffuse_attention(game);

    // 3. Apply deltas
    for (int i = 0; i < n; i++) {
        attention_[i] += scratch_total_deltas_[i];
    }

    // 4. Normalize (subtract mean), clamp
    float mean = std::accumulate(attention_.begin(), attention_.end(), 0.0f)
                 / static_cast<float>(n);
    for (int i = 0; i < n; i++) {
        attention_[i] -= mean;
        attention_[i] = std::clamp(attention_[i], -1000.0f, 1000.0f);
    }

    // 5. Build mask from direct commands — only mask source nodes that have
    //    direct commands, so the attention flow doesn't double-spend their troops.
    //    Target nodes (enemy) aren't ours so execute_troop_flow skips them anyway.
    scratch_masked_.assign(n, false);
    for (const TroopCommand& cmd : out.troops) {
        scratch_masked_[cmd.from_node] = true;
    }

    // 6. Gradient-based troop flow (skips masked nodes)
    execute_troop_flow(game, out, scratch_masked_);

    // 7. Compute attention field metrics
    if (metrics_enabled_) {
        compute_attention_metrics(game, player_id, n);
    }
}

void AttentionAIPlayer::compute_attention_metrics(const Game& game, int player_id, int n) {
    metrics_.player_id = player_id;

    // Attention entropy: Shannon entropy of softmax(attention)
    float max_att = *std::max_element(attention_.begin(), attention_.end());
    float min_att = *std::min_element(attention_.begin(), attention_.end());
    float sum_exp = 0.0f;
    for (int i = 0; i < n; i++) {
        sum_exp += std::exp(attention_[i] - max_att);
    }
    float entropy = 0.0f;
    for (int i = 0; i < n; i++) {
        float p = std::exp(attention_[i] - max_att) / sum_exp;
        if (p > 1e-10f) entropy -= p * std::log(p);
    }
    metrics_.attention_entropy = entropy;
    metrics_.attention_max = max_att;
    metrics_.attention_min = min_att;
    metrics_.attention_range = max_att - min_att;

    // Gradient utilization: fraction of owned nodes with at least one higher-attention neighbor
    int owned_count = 0, flowing_count = 0;
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();
    for (int i = 0; i < n; i++) {
        if (nodes_data[i].owner != player_id) continue;
        owned_count++;
        float self = attention_[i];
        for (int nbr : graph.nodes[i].neighbor_indices) {
            if (attention_[nbr] > self) { flowing_count++; break; }
        }
    }
    metrics_.attention_gradient_util = owned_count > 0
        ? static_cast<float>(flowing_count) / static_cast<float>(owned_count) : 0.0f;
}

void AttentionAIPlayer::diffuse_attention(const Game& game) {
    // Sparse Laplacian diffusion: a[i] -= 0.95 * rate * (degree(i)*a[i] - sum(a[j]))
    const Graph& graph = game.graph();
    int n = graph.num_nodes();

    scratch_lap_.resize(n);
    for (int i = 0; i < n; i++) {
        const std::vector<int>& nbrs = graph.nodes[i].neighbor_indices;
        float degree = static_cast<float>(nbrs.size());
        float nbr_sum = 0.0f;
        for (int j : nbrs) {
            nbr_sum += attention_[j];
        }
        scratch_lap_[i] = degree * attention_[i] - nbr_sum;
    }

    constexpr float scale = 0.95f * DIFFUSION_RATE;
    for (int i = 0; i < n; i++) {
        attention_[i] -= scale * scratch_lap_[i];
    }
}

void AttentionAIPlayer::execute_troop_flow(const Game& game, PlayerCommands& out,
                                     const std::vector<bool>& masked) {
    // Delegate to extracted transport solver, using attention field as gradient signal.
    auto flow_cmds = transport_solver_greedy(
        game.graph(), game.node_data(), attention_, player_id_,
        masked, OUTFLOW_RATE, MIN_TROOPS_TO_SEND);
    out.troops.insert(out.troops.end(), flow_cmds.begin(), flow_cmds.end());
}
