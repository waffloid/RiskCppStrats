#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <queue>
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

    // Softmax → distribution
    if (config_.use_distance_softmax) {
        // Lazy-compute all-pairs BFS distances (once per game)
        if (dist_matrix_.empty()) {
            const auto& graph = game.graph();
            dist_matrix_.resize(n * n, n);  // default = n (unreachable)
            for (int src = 0; src < n; src++) {
                dist_matrix_[src * n + src] = 0;
                std::queue<int> q;
                q.push(src);
                while (!q.empty()) {
                    int cur = q.front(); q.pop();
                    for (int nbr : graph.neighbors(cur)) {
                        if (dist_matrix_[src * n + nbr] == n) {
                            dist_matrix_[src * n + nbr] = dist_matrix_[src * n + cur] + 1;
                            q.push(nbr);
                        }
                    }
                }
            }
        }
        cur_distribution_ = softmax_distance(combined, config_.global_beta, dist_matrix_, n);
    } else {
        cur_distribution_ = softmax(combined, config_.global_beta);
    }

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

    // Transport: OT (min-cost flow) or greedy gradient-following
    if (use_ot_transport_) {
        const auto& cfg = game.config();
        const auto& graph = game.graph();
        int min_g = config_.transport_min_troops;

        // Build per-node cost map from economy agent's build queue.
        // This gives us the correct structure type (factory vs PP) for each target.
        std::vector<int> node_build_cost(n, 0);
        for (const auto& slot : sub_agents_) {
            auto* econ = dynamic_cast<EconomySubAgent*>(slot.agent.get());
            if (!econ) continue;
            for (const auto& cmd : econ->build_queue()) {
                int cost = (cmd.structure == NodeState::POWERPLANT)
                         ? cfg.cost_powerplant : cfg.cost_factory;
                node_build_cost[cmd.node_idx] = cost + 1;
            }
        }

        // Set OT targets: each node gets demand based on actual troop needs.
        std::vector<int> targets(n, 0);
        int frontline_g = config_.ot_frontline_garrison;
        for (int i = 0; i < n; i++) {
            if (nodes_data[i].owner == player_id) {
                if (node_build_cost[i] > 0) {
                    targets[i] = node_build_cost[i];
                } else {
                    targets[i] = min_g;
                }
                // Boost garrison for nodes adjacent to real enemies:
                // demand 1.2x the max enemy neighbor's troops
                if (frontline_g > 0) {
                    int max_enemy_troops = 0;
                    for (int nbr : graph.neighbors(i)) {
                        int nbr_owner = nodes_data[nbr].owner;
                        if (nbr_owner >= 0 && nbr_owner != player_id
                            && nbr_owner < game.n_real_players()) {
                            int enemy_troops = nodes_data[nbr].troops[nbr_owner];
                            if (enemy_troops > max_enemy_troops)
                                max_enemy_troops = enemy_troops;
                        }
                    }
                    if (max_enemy_troops > 0) {
                        int want = static_cast<int>(max_enemy_troops * 1.2f) + 1;
                        targets[i] = std::max(targets[i], want);
                    }
                }
            } else if (combined[i] > 0.0f) {
                // Skip nodes owned by real enemy players — war agent
                // handles those via direct commands with feasibility checks.
                int owner = nodes_data[i].owner;
                if (owner >= 0 && owner < game.n_real_players()) continue;

                // Expansion (neutral/unowned): need enough to beat defenders
                int defenders = 0;
                for (int p = 0; p < static_cast<int>(nodes_data[i].troops.size()); p++)
                    defenders += nodes_data[i].troops[p];
                targets[i] = defenders + 1;
            }
        }

        // Compute per-node value for cost weighting in OT LP.
        // Value = marginal production improvement from building here.
        // SSP edge cost becomes dist / value, so high-value targets are
        // effectively closer and get served first.
        std::vector<float> demand_value(n, 0.0f);
        for (int i = 0; i < n; i++) {
            if (targets[i] <= 0) continue;
            if (nodes_data[i].owner == player_id && node_build_cost[i] > 0) {
                // Economy build target — check what structure
                bool is_pp = (node_build_cost[i] > cfg.cost_factory + 1);
                if (is_pp) {
                    // PP value: 2 * number of adjacent built factories
                    int factory_nbrs = 0;
                    for (int nbr : graph.neighbors(i)) {
                        if (nodes_data[nbr].owner == player_id &&
                            (nodes_data[nbr].state == NodeState::FACTORY ||
                             nodes_data[nbr].state == NodeState::CAPITAL))
                            factory_nbrs++;
                    }
                    demand_value[i] = std::max(1.0f, 2.0f * factory_nbrs);
                } else {
                    // Factory value: 2 * number of adjacent PPs + 1
                    int pp_nbrs = 0;
                    for (int nbr : graph.neighbors(i)) {
                        if (nodes_data[nbr].owner == player_id &&
                            nodes_data[nbr].state == NodeState::POWERPLANT)
                            pp_nbrs++;
                    }
                    demand_value[i] = 1.0f + 2.0f * pp_nbrs;
                }
            } else {
                // Expansion or garrison — base value
                demand_value[i] = 0.5f;
            }
        }

        // Compute per-node production rate (troops/tick) for saturation tranches
        std::vector<float> prod_rate(n, 0.0f);
        for (int i = 0; i < n; i++) {
            if (nodes_data[i].owner != player_id) continue;
            int base = 0;
            if (nodes_data[i].state == NodeState::CAPITAL)
                base = cfg.capital_troops_per_tick;
            else if (nodes_data[i].state == NodeState::FACTORY)
                base = cfg.factory_troops_per_tick;
            if (base > 0) {
                for (int nbr : graph.neighbors(i)) {
                    if (nodes_data[nbr].state == NodeState::POWERPLANT &&
                        nodes_data[nbr].owner == player_id)
                        base += cfg.powerplant_bonus;
                }
            }
            prod_rate[i] = static_cast<float>(base);
        }

        // Lazy-init NetworkSimplex (once per game, reused with warm start)
        if (!ns_)
            ns_ = std::make_unique<NetworkSimplex>(graph);

        // effective_troops: include in-transit when enabled, empty otherwise
        std::vector<float> eff_troops;
        if (use_effective_troops_)
            eff_troops = game.effective_troops(player_id);

        auto ot_cmds = ns_->solve(game.node_data(), player_id, scratch_masked_,
                                  targets, demand_value, eff_troops, prod_rate,
                                  config_.ot_saturation_alpha,
                                  config_.ot_value_alpha,
                                  config_.ot_fw_iterations);
        for (auto& cmd : ot_cmds) {
            if (cmd.count > 0) out.troops.push_back(cmd);
        }
    } else {
        auto transport_cmds = transport_solver_greedy(
            game.graph(), game.node_data(), potential, player_id,
            scratch_masked_, config_.transport_outflow_rate, config_.transport_min_troops);
        for (auto& cmd : transport_cmds) {
            out.troops.push_back(cmd);
        }
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
