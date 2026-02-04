#include "player/attention_ai.hpp"
#include "player/economy_agent.hpp"
#include "player/expansion_agent.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

AttentionAI::AttentionAI(int player_id) : player_id_(player_id) {
    // Default sub-agents matching original behavior
    sub_agents_.push_back({std::make_unique<EconomySubAgent>(), 1.0f});
    sub_agents_.push_back({std::make_unique<ExpansionSubAgent>(), 1.0f});
}

AttentionAI::AttentionAI(int player_id, NoDefaults) : player_id_(player_id) {
    // No sub-agents — caller adds their own
}

void AttentionAI::add_sub_agent(std::unique_ptr<AttentionSubAgent> agent, float weight) {
    sub_agents_.push_back({std::move(agent), weight});
}

void AttentionAI::decide(const Game& game, int player_id, PlayerCommands& out) {
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
}

void AttentionAI::diffuse_attention(const Game& game) {
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

void AttentionAI::execute_troop_flow(const Game& game, PlayerCommands& out,
                                     const std::vector<bool>& masked) {
    const std::vector<NodeData>& nodes_data = game.node_data();
    const Graph& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const NodeData& nd = nodes_data[node];
        if (nd.owner != player_id_) continue;
        if (masked[node]) continue;  // claimed by a sub-agent's direct commands

        int troops_here = nd.troops[player_id_];
        if (troops_here < MIN_TROOPS_TO_SEND * 2) continue;

        const std::vector<int>& nbrs = graph.nodes[node].neighbor_indices;
        if (nbrs.empty()) continue;

        // Compute attention differences (positive part only)
        float self_att = attention_[node];
        scratch_pos_diff_.resize(nbrs.size());
        float norm_sq = 0.0f;
        for (size_t i = 0; i < nbrs.size(); i++) {
            float diff = attention_[nbrs[i]] - self_att;
            scratch_pos_diff_[i] = (diff > 0.0f) ? diff : 0.0f;
            norm_sq += scratch_pos_diff_[i] * scratch_pos_diff_[i];
        }

        float norm_pos = std::sqrt(norm_sq);
        if (norm_pos < 1e-6f) continue;

        // Outflow fraction: sigmoid(norm) * OUTFLOW_RATE
        float clamped = std::min(norm_pos, 20.0f);
        float exp_val = std::exp(clamped);
        float outflow_fraction = (exp_val / (1.0f + exp_val)) * OUTFLOW_RATE;

        float total_to_send = static_cast<float>(troops_here) * outflow_fraction;
        if (total_to_send < static_cast<float>(MIN_TROOPS_TO_SEND)) continue;

        // Send proportionally to each neighbor
        for (size_t i = 0; i < nbrs.size(); i++) {
            float allocation = scratch_pos_diff_[i] / norm_pos;
            int amount = static_cast<int>(total_to_send * allocation);
            if (amount >= MIN_TROOPS_TO_SEND) {
                out.troops.push_back({node, nbrs[i], amount});
            }
        }
    }
}
