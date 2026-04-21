#include "systems/ordering/ordering_solvers.hpp"
#include "systems/economy/economy_solvers.hpp"  // compute_production_rate

#include <algorithm>
#include <climits>
#include <numeric>
#include <queue>

std::vector<int> ordering_solver_sequential(
    const BuildPlan& plan, const Graph&, const std::vector<NodeData>&,
    int, const GameConfig&) {

    std::vector<int> order(plan.steps.size());
    std::iota(order.begin(), order.end(), 0);
    return order;
}

std::vector<int> ordering_solver_cheapest_first(
    const BuildPlan& plan, const Graph&, const std::vector<NodeData>&,
    int, const GameConfig& config) {

    std::vector<int> order(plan.steps.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return building_cost(plan.steps[a].structure, config)
             < building_cost(plan.steps[b].structure, config);
    });
    return order;
}

std::vector<int> ordering_solver_nearest_first(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig&) {

    // Find capital
    int capital = 0;
    for (int i = 0; i < graph.num_nodes(); i++) {
        if (nodes[i].owner == player_id && nodes[i].state == NodeState::CAPITAL) {
            capital = i;
            break;
        }
    }

    // BFS from capital
    std::vector<int> dist(graph.num_nodes(), INT_MAX);
    dist[capital] = 0;
    std::queue<int> q;
    q.push(capital);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        for (int v : graph.neighbors(u)) {
            if (dist[v] == INT_MAX) {
                dist[v] = dist[u] + 1;
                q.push(v);
            }
        }
    }

    std::vector<int> order(plan.steps.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return dist[plan.steps[a].node_idx] < dist[plan.steps[b].node_idx];
    });
    return order;
}

std::vector<int> ordering_solver_production_gradient(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config) {

    // Greedy: at each step, pick the unbuilt step that maximizes production increase.
    std::vector<int> order;
    std::vector<bool> used(plan.steps.size(), false);
    std::vector<NodeData> sim_nodes = nodes;  // working copy

    for (size_t step = 0; step < plan.steps.size(); step++) {
        float current_rate = compute_production_rate(graph, sim_nodes, player_id, config);
        int best = -1;
        float best_delta = -1e9f;

        for (size_t i = 0; i < plan.steps.size(); i++) {
            if (used[i]) continue;

            // Tentatively build
            NodeState prev = sim_nodes[plan.steps[i].node_idx].state;
            sim_nodes[plan.steps[i].node_idx].state = plan.steps[i].structure;
            float new_rate = compute_production_rate(graph, sim_nodes, player_id, config);
            float delta = new_rate - current_rate;
            sim_nodes[plan.steps[i].node_idx].state = prev;  // undo

            if (best == -1 || delta > best_delta) {
                best_delta = delta;
                best = static_cast<int>(i);
            }
        }

        order.push_back(best);
        used[best] = true;
        sim_nodes[plan.steps[best].node_idx].state = plan.steps[best].structure;
    }

    return order;
}

OrderingSolver get_ordering_solver(const std::string& name) {
    if (name == "sequential") return ordering_solver_sequential;
    if (name == "cheapest_first") return ordering_solver_cheapest_first;
    if (name == "nearest_first") return ordering_solver_nearest_first;
    if (name == "production_gradient") return ordering_solver_production_gradient;
    return nullptr;
}

std::vector<std::string> list_ordering_solvers() {
    return {"sequential", "cheapest_first", "nearest_first", "production_gradient"};
}
