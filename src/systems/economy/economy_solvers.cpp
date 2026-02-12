#include "systems/economy/economy_solvers.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

// ── Greedy solver ──────────────────────────────────────────────
// Extracted from EconomySubAgent::contribute() (economy_agent.cpp:46-52).

BuildPlan economy_solver_greedy(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config) {

    BuildPlan plan;

    for (int node = 0; node < graph.num_nodes(); node++) {
        const auto& nd = nodes[node];
        if (nd.owner != player_id) continue;
        if (nd.state == NodeState::CAPITAL) continue;

        const auto& nbrs = graph.neighbors(node);

        // Count adjacent factories/capitals and powerplants owned by us.
        int factory_neighbors = 0;
        int powerplant_neighbors = 0;
        for (int nbr : nbrs) {
            const auto& nbd = nodes[nbr];
            if (nbd.owner == player_id) {
                if (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL)
                    factory_neighbors++;
                if (nbd.state == NodeState::POWERPLANT)
                    powerplant_neighbors++;
            }
        }

        int balance = factory_neighbors - powerplant_neighbors;

        int troops = nd.troops[player_id];
        if (troops > config.cost_powerplant && nd.state != NodeState::POWERPLANT && balance > 0) {
            plan.steps.push_back({node, NodeState::POWERPLANT});
        } else if (troops > config.cost_factory && nd.state != NodeState::FACTORY
                   && nd.state != NodeState::POWERPLANT && balance <= 0) {
            plan.steps.push_back({node, NodeState::FACTORY});
        }
    }

    plan.estimated_production = compute_production_rate(graph, nodes, player_id, config);
    return plan;
}

// ── Bootstrap solver ───────────────────────────────────────────
// Extracted from BootstrapEconomySubAgent::generate_plan()
// (bootstrap_economy_agent.cpp:8-95).

BuildPlan economy_solver_bootstrap(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config,
    int num_factories) {

    BuildPlan plan;

    // Find capital.
    int capital = -1;
    for (int i = 0; i < graph.num_nodes(); i++) {
        if (nodes[i].state == NodeState::CAPITAL && nodes[i].owner == player_id) {
            capital = i;
            break;
        }
    }
    if (capital < 0) return plan;

    // BFS from capital to get nodes sorted by hop distance.
    std::vector<int> bfs_order;
    std::vector<bool> visited(graph.num_nodes(), false);
    std::queue<int> q;
    q.push(capital);
    visited[capital] = true;
    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        bfs_order.push_back(cur);
        for (int nbr : graph.neighbors(cur)) {
            if (!visited[nbr]) {
                visited[nbr] = true;
                q.push(nbr);
            }
        }
    }

    // Pick first num_factories non-capital nodes in BFS order as factory sites.
    std::unordered_set<int> factory_nodes;
    for (int node : bfs_order) {
        if (node == capital) continue;
        if (static_cast<int>(factory_nodes.size()) >= num_factories) break;
        factory_nodes.insert(node);
        plan.steps.push_back({node, NodeState::FACTORY});
    }

    // Pick powerplant: adjacent to at least one factory/capital, scored by
    // producer count with connectivity tiebreaker.
    int best_pp = -1;
    float best_score = -1.0f;

    for (int node : bfs_order) {
        if (node == capital) continue;
        if (factory_nodes.count(node)) continue;

        bool adj_to_producer = false;
        for (int nbr : graph.neighbors(node)) {
            if (factory_nodes.count(nbr) || nbr == capital) {
                adj_to_producer = true;
                break;
            }
        }
        if (!adj_to_producer) continue;

        constexpr float k = 0.3f;
        float producer_count = 0.0f;
        float connectivity = 0.0f;
        for (int nbr : graph.neighbors(node)) {
            if (factory_nodes.count(nbr) || nbr == capital) {
                producer_count += 1.0f;
            }
            int eidx = graph.edge_between(node, nbr);
            if (eidx >= 0) {
                float len = graph.edges[eidx].length;
                connectivity += 1.0f + k / std::max(len, 0.01f);
            }
        }
        float score = producer_count * 100.0f + connectivity;

        if (score > best_score) {
            best_score = score;
            best_pp = node;
        }
    }

    if (best_pp >= 0) {
        plan.steps.push_back({best_pp, NodeState::POWERPLANT});
    }

    plan.estimated_production = compute_production_rate(graph, nodes, player_id, config);
    return plan;
}

// ── Stubs ──────────────────────────────────────────────────────

BuildPlan economy_solver_mcmc(
    const Graph& /*graph*/,
    const std::vector<NodeData>& /*nodes*/,
    int /*player_id*/,
    const GameConfig& /*config*/) {
    // TODO: MCMC over assignments, maximize production rate.
    return BuildPlan{};
}

BuildPlan economy_solver_branch_bound(
    const Graph& /*graph*/,
    const std::vector<NodeData>& /*nodes*/,
    int /*player_id*/,
    const GameConfig& /*config*/) {
    // TODO: Exact QP solve for small graphs.
    return BuildPlan{};
}

// ── Utility ────────────────────────────────────────────────────

float compute_production_rate(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config) {

    float production = 0.0f;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& nd = nodes[i];
        if (nd.owner != player_id) continue;

        float base = 0.0f;
        if (nd.state == NodeState::CAPITAL) {
            base = static_cast<float>(config.capital_troops_per_tick);
        } else if (nd.state == NodeState::FACTORY) {
            base = static_cast<float>(config.factory_troops_per_tick);
        } else {
            continue;  // only producers count
        }

        // Check if powered by adjacent powerplant.
        bool powered = false;
        for (int nbr : graph.neighbors(i)) {
            if (nodes[nbr].state == NodeState::POWERPLANT && nodes[nbr].owner == player_id) {
                powered = true;
                break;
            }
        }

        production += base + (powered ? static_cast<float>(config.powerplant_bonus) : 0.0f);
    }

    return production;
}

float compute_theoretical_max_production(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config) {

    float max_prod = 0.0f;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& nd = nodes[i];
        if (nd.owner != player_id) continue;

        if (nd.state == NodeState::CAPITAL) {
            max_prod += static_cast<float>(config.capital_troops_per_tick + config.powerplant_bonus);
        } else if (nd.state == NodeState::FACTORY) {
            max_prod += static_cast<float>(config.factory_troops_per_tick + config.powerplant_bonus);
        }
    }

    return max_prod;
}
