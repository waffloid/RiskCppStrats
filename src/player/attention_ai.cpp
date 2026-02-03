#include "player/attention_ai.hpp"
#include "engine/game.hpp"

#include <cmath>
#include <algorithm>
#include <numeric>

AttentionAI::AttentionAI(int player_id) : player_id_(player_id) {}

void AttentionAI::decide(const Game& game, int player_id, PlayerCommands& out) {
    if (!game.is_alive(player_id)) return;

    int n = game.graph().num_nodes();

    // Lazy-init attention vector
    if (!initialized_) {
        attention_.assign(n, 0.0f);
        initialized_ = true;
    }

    // 1. Generate attention deltas
    std::vector<float> deltas(n, 0.0f);
    generate_attention_deltas(game, deltas);

    // 2. Diffuse attention (sparse Laplacian, no matrix)
    diffuse_attention(game);

    // 3. Apply deltas
    for (int i = 0; i < n; i++) {
        attention_[i] += deltas[i];
    }

    // 4. Normalize (subtract mean), clamp
    float mean = std::accumulate(attention_.begin(), attention_.end(), 0.0f)
                 / static_cast<float>(n);
    for (int i = 0; i < n; i++) {
        attention_[i] -= mean;
        attention_[i] = std::clamp(attention_[i], -1000.0f, 1000.0f);
    }

    // 5. Gradient-based troop flow
    execute_troop_flow(game, out);

    // 6. Build structures
    try_build_structures(game, out);
}

void AttentionAI::generate_attention_deltas(const Game& game, std::vector<float>& deltas) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        const auto& nbrs = graph.nodes[node].neighbor_indices;

        if (nd.state == NodeState::CAPITAL) continue;

        if (nd.owner == player_id_) {
            // Owned node: powerplant/factory desire heuristic
            if (nd.state == NodeState::DEFAULT || nd.state == NodeState::FACTORY) {
                int factory_neighbors = 0;
                int powerplant_neighbors = 0;
                for (int nbr : nbrs) {
                    const auto& nbd = nodes_data[nbr];
                    if (nbd.owner == player_id_) {
                        if (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL)
                            factory_neighbors++;
                        if (nbd.state == NodeState::POWERPLANT)
                            powerplant_neighbors++;
                    }
                }
                deltas[node] += static_cast<float>(factory_neighbors - powerplant_neighbors)
                                * ATTENTION_FACTORY_POWERPLANT_DELTA_DESIRE;

                if (nd.state == NodeState::DEFAULT) {
                    deltas[node] += ATTENTION_UNOCCUPIED_BORDER;
                }
            }
        } else {
            // Non-owned node (unowned OR enemy/neutral) adjacent to our territory
            bool adjacent_to_us = false;
            for (int nbr : nbrs) {
                if (nodes_data[nbr].owner == player_id_) {
                    adjacent_to_us = true;
                    break;
                }
            }
            if (adjacent_to_us) {
                deltas[node] += ATTENTION_UNOCCUPIED_BORDER;
            }
        }
    }
}

void AttentionAI::diffuse_attention(const Game& game) {
    // Sparse Laplacian diffusion: a[i] -= 0.95 * rate * (degree(i)*a[i] - sum(a[j]))
    // Computed on-the-fly from adjacency lists — O(edges), zero extra memory.
    const auto& graph = game.graph();
    int n = graph.num_nodes();

    // We need a temporary copy since diffusion reads neighbors' old values
    std::vector<float> lap(n);
    for (int i = 0; i < n; i++) {
        const auto& nbrs = graph.nodes[i].neighbor_indices;
        float degree = static_cast<float>(nbrs.size());
        float nbr_sum = 0.0f;
        for (int j : nbrs) {
            nbr_sum += attention_[j];
        }
        lap[i] = degree * attention_[i] - nbr_sum;  // L @ attention for row i
    }

    constexpr float scale = 0.95f * DIFFUSION_RATE;
    for (int i = 0; i < n; i++) {
        attention_[i] -= scale * lap[i];
    }
}

void AttentionAI::execute_troop_flow(const Game& game, PlayerCommands& out) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        if (nd.owner != player_id_) continue;

        int troops_here = nd.troops[player_id_];
        if (troops_here < MIN_TROOPS_TO_SEND * 2) continue;

        const auto& nbrs = graph.nodes[node].neighbor_indices;
        if (nbrs.empty()) continue;

        // Compute attention differences (positive part only)
        float self_att = attention_[node];
        std::vector<float> pos_diff(nbrs.size());
        float norm_sq = 0.0f;
        for (size_t i = 0; i < nbrs.size(); i++) {
            float diff = attention_[nbrs[i]] - self_att;
            pos_diff[i] = (diff > 0.0f) ? diff : 0.0f;
            norm_sq += pos_diff[i] * pos_diff[i];
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
            float allocation = pos_diff[i] / norm_pos;
            int amount = static_cast<int>(total_to_send * allocation);
            if (amount >= MIN_TROOPS_TO_SEND) {
                out.troops.push_back({node, nbrs[i], amount});
            }
        }
    }
}

void AttentionAI::try_build_structures(const Game& game, PlayerCommands& out) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();
    const int cost_factory = game.config().cost_factory;
    const int cost_powerplant = game.config().cost_powerplant;

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes_data[node];
        if (nd.owner != player_id_) continue;

        int troops = nd.troops[player_id_];
        const auto& nbrs = graph.nodes[node].neighbor_indices;

        // Count adjacent factories/capitals and powerplants
        int adjacent_production = 0;
        int adjacent_powerplants = 0;
        for (int nbr : nbrs) {
            const auto& nbd = nodes_data[nbr];
            if (nbd.owner == player_id_) {
                if (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL)
                    adjacent_production++;
                if (nbd.state == NodeState::POWERPLANT)
                    adjacent_powerplants++;
            }
        }

        int balance = adjacent_production - adjacent_powerplants;

        if (troops > cost_powerplant && nd.state != NodeState::POWERPLANT && balance > 0) {
            out.builds.push_back({node, NodeState::POWERPLANT});
        } else if (troops > cost_factory && nd.state != NodeState::FACTORY
                   && nd.state != NodeState::POWERPLANT && balance <= 0) {
            // Only build factory when balance <= 0 (matching Python logic for the else-if)
            // Python: elif ... and ((not state == POWERPLANT) or factory_pp_balance < 0)
            // Simplified: don't overwrite powerplant unless balance < 0
            out.builds.push_back({node, NodeState::FACTORY});
        }
    }
}
