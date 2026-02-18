#include "ai/sub_agents/economy_agent.hpp"
#include "engine/game.hpp"
#include "systems/economy/economy_solvers.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

void EconomySubAgent::compute_plan(const Game& game, int player_id) {
    // Run solver once on the full map (pretend we own everything)
    // to get the globally optimal factory/PP layout.
    const auto& graph = game.graph();
    auto nodes_copy = game.node_data();
    for (auto& nd : nodes_copy) {
        nd.owner = player_id;
    }

    BuildPlan plan;
    if (solver_) {
        plan = solver_(graph, nodes_copy, player_id, game.config());
    } else {
        plan = economy_solver_mcmc_traced(
            graph, nodes_copy, player_id, game.config(),
            10000, 2.0f, nullptr);
    }

    ideal_plan_ = std::move(plan.steps);
    plan_computed_ = true;
}

void EconomySubAgent::rebuild_queue(const Game& game, int player_id) {
    const auto& nodes_data = game.node_data();
    const auto& graph = game.graph();

    // What's already in the queue?
    std::unordered_set<int> queued;
    for (const auto& cmd : queue_) queued.insert(cmd.node_idx);

    // BFS distance from existing infrastructure (for nearest-first ordering)
    std::vector<int> bfs_dist(graph.num_nodes(), -1);
    std::queue<int> bfs;
    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& nd = nodes_data[i];
        if (nd.owner == player_id &&
            (nd.state == NodeState::CAPITAL ||
             nd.state == NodeState::FACTORY ||
             nd.state == NodeState::POWERPLANT)) {
            bfs_dist[i] = 0;
            bfs.push(i);
        }
    }
    while (!bfs.empty()) {
        int cur = bfs.front();
        bfs.pop();
        for (int nbr : graph.neighbors(cur)) {
            if (bfs_dist[nbr] < 0 && nodes_data[nbr].owner == player_id) {
                bfs_dist[nbr] = bfs_dist[cur] + 1;
                bfs.push(nbr);
            }
        }
    }

    // Cap queue size
    int num_factories = 0;
    for (const auto& nd : nodes_data) {
        if (nd.owner == player_id && nd.state == NodeState::FACTORY) num_factories++;
    }
    int max_queue = 1 + num_factories / 5;

    // Greedily fill empty slots from ideal plan.
    // Priority: feasible PP > factory. Within each, nearest first.
    // A PP is feasible if it has at least 1 adjacent built factory/capital.

    // Collect candidates from ideal plan that are owned, unbuilt, not already queued
    std::vector<BuildCommand> pp_candidates;
    std::vector<BuildCommand> factory_candidates;

    for (const auto& step : ideal_plan_) {
        if (queued.count(step.node_idx)) continue;
        const auto& nd = nodes_data[step.node_idx];

        // Only target owned DEFAULT nodes (ready to build).
        // Expansion handles capturing; economy builds on what we own.
        if (nd.owner != player_id || nd.state != NodeState::DEFAULT) continue;

        if (step.structure == NodeState::POWERPLANT) {
            // Count how many neighbors are factories in the ideal plan
            int planned_producers = 0;
            for (int nbr : graph.neighbors(step.node_idx)) {
                for (const auto& ps : ideal_plan_) {
                    if (ps.node_idx == nbr && ps.structure == NodeState::FACTORY) {
                        planned_producers++;
                        break;
                    }
                }
                // Capital also counts as a planned producer
                if (nodes_data[nbr].state == NodeState::CAPITAL) {
                    planned_producers++;
                }
            }
            int threshold = std::min(planned_producers, 4);

            // Count how many are actually built
            int built_producers = 0;
            for (int nbr : graph.neighbors(step.node_idx)) {
                const auto& nbd = nodes_data[nbr];
                if (nbd.owner == player_id &&
                    (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL)) {
                    built_producers++;
                }
            }
            if (built_producers >= 3 && built_producers >= threshold) pp_candidates.push_back(step);
        } else {
            factory_candidates.push_back(step);
        }
    }

    // Sort each group by BFS distance (nearest first)
    auto by_dist = [&](const BuildCommand& a, const BuildCommand& b) {
        int da = bfs_dist[a.node_idx] >= 0 ? bfs_dist[a.node_idx] : 9999;
        int db = bfs_dist[b.node_idx] >= 0 ? bfs_dist[b.node_idx] : 9999;
        return da < db;
    };
    std::sort(pp_candidates.begin(), pp_candidates.end(), by_dist);
    std::sort(factory_candidates.begin(), factory_candidates.end(), by_dist);

    // Fill: PPs first (high value when feasible), then factories
    for (const auto& cmd : pp_candidates) {
        if (static_cast<int>(queue_.size()) >= max_queue) break;
        queue_.push_back(cmd);
    }
    for (const auto& cmd : factory_candidates) {
        if (static_cast<int>(queue_.size()) >= max_queue) break;
        queue_.push_back(cmd);
    }

    // Fallback: if queue is still empty, try owned DEFAULT nodes first,
    // then bootstrap with the single best capturable neighbor from MCMC plan.
    if (queue_.empty()) {
        std::vector<std::pair<int, int>> fallbacks; // (bfs_dist, node_idx)
        for (int i = 0; i < graph.num_nodes(); i++) {
            if (queued.count(i)) continue;
            const auto& nd = nodes_data[i];
            if (nd.owner != player_id || nd.state != NodeState::DEFAULT) continue;
            int d = bfs_dist[i] >= 0 ? bfs_dist[i] : 9999;
            fallbacks.push_back({d, i});
        }
        std::sort(fallbacks.begin(), fallbacks.end());
        for (const auto& [d, idx] : fallbacks) {
            if (static_cast<int>(queue_.size()) >= max_queue) break;
            queue_.push_back({idx, NodeState::FACTORY});
        }
    }

    // Bootstrap: if still empty (no owned DEFAULT nodes at all),
    // pick the best adjacent capturable node as a factory target.
    // Don't require it to be in MCMC plan — the goal is to capture and build
    // SOMETHING nearby, directing expansion instead of scattering.
    if (queue_.empty()) {
        int best_node = -1;
        int best_nbrs = -1;
        for (int i = 0; i < graph.num_nodes(); i++) {
            const auto& nd = nodes_data[i];
            if (nd.owner == player_id) continue;
            if (nd.state != NodeState::DEFAULT) continue;
            int owned_nbrs = 0;
            for (int nbr : graph.neighbors(i)) {
                if (nodes_data[nbr].owner == player_id) owned_nbrs++;
            }
            if (owned_nbrs == 0) continue; // not adjacent to territory
            if (owned_nbrs > best_nbrs) {
                best_nbrs = owned_nbrs;
                best_node = i;
            }
        }
        if (best_node >= 0) {
            queue_.push_back({best_node, NodeState::FACTORY});
        }
    }
}

void EconomySubAgent::score(const Game& game, int player_id,
                             std::vector<float>& scores_out,
                             PlayerCommands& direct_commands_out) {
    const auto& nodes_data = game.node_data();
    const auto& cfg = game.config();

    // One-time MCMC solve
    if (!plan_computed_) {
        compute_plan(game, player_id);
    }

    // Prune entries that are built or completely disconnected from our territory
    const auto& graph = game.graph();
    queue_.erase(
        std::remove_if(queue_.begin(), queue_.end(),
            [&](const BuildCommand& cmd) {
                const auto& nd = nodes_data[cmd.node_idx];
                // Keep only owned DEFAULT nodes
                return nd.owner != player_id || nd.state != NodeState::DEFAULT;
            }),
        queue_.end());

    // Greedily fill empty slots
    int num_factories = 0;
    for (const auto& nd : nodes_data) {
        if (nd.owner == player_id && nd.state == NodeState::FACTORY) num_factories++;
    }
    int max_queue = 1 + num_factories / 5;
    if (static_cast<int>(queue_.size()) < max_queue) {
        rebuild_queue(game, player_id);
    }

    // Emit scores with linear (constant additive) decay down the queue.
    // Softmax exponentiates, so constant additive difference → constant
    // multiplicative ratio in the output distribution.
    int K = static_cast<int>(queue_.size());
    for (int i = 0; i < K; i++) {
        const auto& cmd = queue_[i];
        float base;
        if (cmd.structure == NodeState::POWERPLANT) {
            base = config_.economy_powerplant_score;
        } else {
            int owned_nbrs = 0;
            for (int nbr : graph.neighbors(cmd.node_idx)) {
                if (nodes_data[nbr].owner == player_id) owned_nbrs++;
            }
            base = config_.economy_factory_score * (1 + owned_nbrs);
        }
        // Linear rank weight: 1.0 for first, decreasing to ~0.5 for last
        float rank_weight = (K <= 1) ? 1.0f
            : 1.0f - 0.5f * static_cast<float>(i) / static_cast<float>(K - 1);
        scores_out[cmd.node_idx] = base * rank_weight;
    }

    // Parasitic building: build at any queue target that has enough troops
    for (const auto& cmd : queue_) {
        const auto& nd = nodes_data[cmd.node_idx];
        int troops = nd.troops[player_id];
        int cost = (cmd.structure == NodeState::POWERPLANT)
                 ? cfg.cost_powerplant : cfg.cost_factory;
        if (troops > cost) {
            direct_commands_out.builds.push_back(cmd);
        }
    }
}
