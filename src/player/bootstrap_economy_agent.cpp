#include "player/bootstrap_economy_agent.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

void BootstrapEconomySubAgent::generate_plan(const Game& game, int player_id) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();

    // Find capital
    int capital = -1;
    for (int i = 0; i < graph.num_nodes(); i++) {
        if (nodes_data[i].state == NodeState::CAPITAL && nodes_data[i].owner == player_id) {
            capital = i;
            break;
        }
    }
    if (capital < 0) return;

    // BFS from capital to get nodes sorted by hop distance
    std::vector<int> bfs_order;
    std::vector<bool> visited(graph.num_nodes(), false);
    std::queue<int> q;
    q.push(capital);
    visited[capital] = true;
    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        bfs_order.push_back(cur);
        for (int nbr : graph.nodes[cur].neighbor_indices) {
            if (!visited[nbr]) {
                visited[nbr] = true;
                q.push(nbr);
            }
        }
    }

    // Pick the first NUM_FACTORIES non-capital nodes in BFS order as factory sites
    std::unordered_set<int> factory_nodes;
    for (int node : bfs_order) {
        if (node == capital) continue;
        if (static_cast<int>(factory_nodes.size()) >= NUM_FACTORIES) break;
        factory_nodes.insert(node);
        plan_.push_back({node, NodeState::FACTORY});
    }

    // Pick powerplant: node adjacent to at least one factory, not a factory itself,
    // not the capital. Score by neighbor count discounted by edge distance.
    int best_pp = -1;
    float best_score = -1.0f;

    for (int node : bfs_order) {
        if (node == capital) continue;
        if (factory_nodes.count(node)) continue;

        // Must be adjacent to at least one planned factory or the capital
        bool adj_to_producer = false;
        for (int nbr : graph.nodes[node].neighbor_indices) {
            if (factory_nodes.count(nbr) || nbr == capital) {
                adj_to_producer = true;
                break;
            }
        }
        if (!adj_to_producer) continue;

        // Score: count of adjacent factories/capital (the producers it will boost),
        // with a tiebreaker from regularized inverse edge length for all neighbors
        constexpr float k = 0.3f;
        float producer_count = 0.0f;
        float connectivity = 0.0f;
        for (int nbr : graph.nodes[node].neighbor_indices) {
            if (factory_nodes.count(nbr) || nbr == capital) {
                producer_count += 1.0f;
            }
            int eidx = graph.edge_between(node, nbr);
            if (eidx >= 0) {
                float len = graph.edges[eidx].length;
                connectivity += 1.0f + k / std::max(len, 0.01f);
            }
        }
        // Producers dominate; connectivity is a tiebreaker
        float score = producer_count * 100.0f + connectivity;

        if (score > best_score) {
            best_score = score;
            best_pp = node;
        }
    }

    if (best_pp >= 0) {
        plan_.push_back({best_pp, NodeState::POWERPLANT});
    }
}

void BootstrapEconomySubAgent::contribute(const Game& game, int player_id,
                                           const std::vector<float>& current_attention,
                                           std::vector<float>& deltas_out,
                                           PlayerCommands& direct_commands_out) {
    if (!planned_) {
        generate_plan(game, player_id);
        planned_ = true;
    }

    const auto& nodes_data = game.node_data();
    const auto& config = game.config();

    // Advance cursor past completed steps
    while (plan_cursor_ < static_cast<int>(plan_.size())) {
        const BuildStep& step = plan_[plan_cursor_];
        if (nodes_data[step.node].state == step.structure) {
            plan_cursor_++;
        } else {
            break;
        }
    }

    if (plan_cursor_ < static_cast<int>(plan_.size())) {
        // Bootstrap phase: focus all attention on the current target,
        // suppress attention elsewhere to prevent troop scatter
        const BuildStep& step = plan_[plan_cursor_];
        deltas_out[step.node] += BOOTSTRAP_ATTENTION;

        const auto& graph = game.graph();
        for (int i = 0; i < graph.num_nodes(); i++) {
            if (i != step.node && nodes_data[i].owner == player_id) {
                deltas_out[i] -= BOOTSTRAP_DAMPEN;
            }
        }

        // Build if we own the node and have enough troops
        if (nodes_data[step.node].owner == player_id) {
            int troops = nodes_data[step.node].troops[player_id];
            int cost = (step.structure == NodeState::FACTORY)
                       ? config.cost_factory
                       : config.cost_powerplant;
            if (troops > cost) {
                direct_commands_out.builds.push_back({step.node, step.structure});
            }
        }
    } else {
        // Plan complete — delegate to standard economy logic
        fallback_.contribute(game, player_id, current_attention, deltas_out, direct_commands_out);
    }
}
