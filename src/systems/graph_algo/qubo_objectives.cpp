#include "systems/graph_algo/qubo_objectives.hpp"

#include <queue>
#include <unordered_set>

// ── Helpers ──────────────────────────────────────────────────

// Collect owned non-capital nodes and build variable-index mapping.
static std::pair<std::vector<int>, std::vector<int>> build_variable_map(
    const Graph& graph, const std::vector<NodeData>& nodes, int player_id) {
    std::vector<int> var_to_node;
    std::vector<int> node_to_var(graph.num_nodes(), -1);
    for (int i = 0; i < graph.num_nodes(); i++) {
        if (nodes[i].owner == player_id && nodes[i].state != NodeState::CAPITAL) {
            node_to_var[i] = static_cast<int>(var_to_node.size());
            var_to_node.push_back(i);
        }
    }
    return {var_to_node, node_to_var};
}

// BFS distance from capital for owned nodes.
static std::vector<int> bfs_distances(
    const Graph& graph, const std::vector<NodeData>& nodes, int player_id) {
    int n = graph.num_nodes();
    std::vector<int> dist(n, -1);

    // Find capital
    int capital = -1;
    for (int i = 0; i < n; i++) {
        if (nodes[i].state == NodeState::CAPITAL && nodes[i].owner == player_id) {
            capital = i;
            break;
        }
    }
    if (capital < 0) return dist;

    std::queue<int> q;
    q.push(capital);
    dist[capital] = 0;
    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        for (int nbr : graph.neighbors(cur)) {
            if (dist[nbr] < 0 && nodes[nbr].owner == player_id) {
                dist[nbr] = dist[cur] + 1;
                q.push(nbr);
            }
        }
    }
    return dist;
}

// ── Production objective ─────────────────────────────────────

QUBOEconomyInstance qubo_objective_production(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config) {

    auto [var_to_node, node_to_var] = build_variable_map(graph, nodes, player_id);
    int nv = static_cast<int>(var_to_node.size());

    QUBOInstance qubo;
    qubo.n = nv;
    qubo.Q.assign(nv, std::vector<float>(nv, 0.0f));

    float base = static_cast<float>(config.factory_troops_per_tick);
    float bonus = static_cast<float>(config.powerplant_bonus);

    // The production rate for the economy is:
    //   P = sum_i [is_factory(i) * (base + bonus * count_adjacent_powerplants(i))]
    //     + capital_production (constant, doesn't depend on assignment)
    //
    // With x_i ∈ {-1,+1}, factory(i) = (1+x_i)/2, powerplant(i) = (1-x_i)/2:
    //
    // Factory base production: sum_i base * (1+x_i)/2
    //   = const + (base/2) * sum_i x_i
    //   → linear term: Q[i][i] += base/2
    //
    // Adjacency bonus: for each edge (i,j) between owned nodes,
    //   bonus * base * (1+x_i)(1-x_j)/4  (i=factory, j=powerplant)
    //   + bonus * base * (1+x_j)(1-x_i)/4  (j=factory, i=powerplant)
    //   = bonus * base * (1 - x_i*x_j) / 2
    //   = const + (-bonus*base/2) * x_i * x_j
    //   → quadratic term: Q[i][j] += -bonus*base/2
    //
    // But also the capital contributes adjacency bonuses to powerplants:
    //   For each neighbor j of capital that is a powerplant,
    //   capital gets bonus * capital_rate.
    //   capital_rate * bonus * (1-x_j)/2 = const + (-capital_rate*bonus/2)*x_j
    //   → linear term: Q[j][j] += -capital_rate*bonus/2

    // Linear terms: factories produce, powerplants don't
    for (int vi = 0; vi < nv; vi++) {
        qubo.Q[vi][vi] += base / 2.0f;
    }

    // Quadratic terms: adjacency bonus
    for (int vi = 0; vi < nv; vi++) {
        int ni = var_to_node[vi];
        for (int nbr : graph.neighbors(ni)) {
            int vj = node_to_var[nbr];
            if (vj >= 0 && vj > vi) {
                // Off-diagonal: want opposite signs → negative coupling in max formulation
                // Actually: max x^T Q x, and we want x_i*x_j = -1 to get bonus.
                // Contribution: -bonus*base/2 * x_i * x_j.
                // But wait: x^T Q x uses Q[i][j] + Q[j][i] for off-diagonal.
                // Since Q is symmetric, each Q[i][j] = Q[j][i] = -bonus*base/4.
                // Then the total contribution is 2 * Q[i][j] * x_i * x_j = -bonus*base/2 * x_i*x_j. ✓
                qubo.Q[vi][vj] += -bonus * base / 4.0f;
                qubo.Q[vj][vi] += -bonus * base / 4.0f;
            }

            // Capital adjacency bonus
            if (nodes[nbr].state == NodeState::CAPITAL && nodes[nbr].owner == player_id) {
                float cap_rate = static_cast<float>(config.capital_troops_per_tick);
                qubo.Q[vi][vi] += -cap_rate * bonus / 2.0f;
            }
        }
    }

    return {qubo, var_to_node};
}

// ── Adjacency-only (pure max-cut) ────────────────────────────

QUBOEconomyInstance qubo_objective_adjacency(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& /*config*/) {

    auto [var_to_node, node_to_var] = build_variable_map(graph, nodes, player_id);
    int nv = static_cast<int>(var_to_node.size());

    QUBOInstance qubo;
    qubo.n = nv;
    qubo.Q.assign(nv, std::vector<float>(nv, 0.0f));

    // Pure max-cut: Q[i][j] = -1 for adjacent (want opposite signs → max cut)
    // Actually for max-cut: max sum_{(i,j)} w_ij * (1 - x_i*x_j)/2
    //   = const + sum_{(i,j)} (-w_ij/2) * x_i*x_j
    //   → Q[i][j] = -w_ij/4 (symmetric), so 2*Q[i][j]*x_i*x_j = -w_ij/2 * x_i*x_j
    for (int vi = 0; vi < nv; vi++) {
        int ni = var_to_node[vi];
        for (int nbr : graph.neighbors(ni)) {
            int vj = node_to_var[nbr];
            if (vj >= 0 && vj > vi) {
                qubo.Q[vi][vj] = -0.25f;
                qubo.Q[vj][vi] = -0.25f;
            }
        }
    }

    return {qubo, var_to_node};
}

// ── Distance-weighted ────────────────────────────────────────

QUBOEconomyInstance qubo_objective_distance(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config) {

    // Start from production objective, then weight by distance
    auto result = qubo_objective_production(graph, nodes, player_id, config);
    auto dist = bfs_distances(graph, nodes, player_id);
    int nv = result.qubo.n;

    // Scale Q entries by distance-based decay
    for (int vi = 0; vi < nv; vi++) {
        int ni = result.var_to_node[vi];
        float di = (dist[ni] >= 0) ? 1.0f / (1.0f + static_cast<float>(dist[ni])) : 0.1f;
        // Scale diagonal
        result.qubo.Q[vi][vi] *= di;
        // Scale off-diagonal
        for (int vj = vi + 1; vj < nv; vj++) {
            int nj = result.var_to_node[vj];
            float dj = (dist[nj] >= 0) ? 1.0f / (1.0f + static_cast<float>(dist[nj])) : 0.1f;
            float scale = (di + dj) / 2.0f;
            result.qubo.Q[vi][vj] *= scale;
            result.qubo.Q[vj][vi] *= scale;
        }
    }

    return result;
}

// ── Degree-weighted ──────────────────────────────────────────

QUBOEconomyInstance qubo_objective_degree(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config) {

    // Start from production objective, add degree bias
    auto result = qubo_objective_production(graph, nodes, player_id, config);
    int nv = result.qubo.n;

    float base = static_cast<float>(config.factory_troops_per_tick);

    // High-degree nodes biased toward powerplant (x_i = -1).
    // This is a negative linear bias proportional to degree.
    for (int vi = 0; vi < nv; vi++) {
        int ni = result.var_to_node[vi];
        int deg = graph.degree(ni);
        // Scale: stronger bias for higher degree, relative to base production
        result.qubo.Q[vi][vi] += -base * 0.1f * static_cast<float>(deg);
    }

    return result;
}

// ── Composite ────────────────────────────────────────────────

QUBOEconomyInstance qubo_objective_composite(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config) {

    // Combination: production + distance decay + mild degree bias
    auto prod = qubo_objective_production(graph, nodes, player_id, config);
    auto dist_map = bfs_distances(graph, nodes, player_id);
    int nv = prod.qubo.n;

    float base = static_cast<float>(config.factory_troops_per_tick);

    for (int vi = 0; vi < nv; vi++) {
        int ni = prod.var_to_node[vi];

        // Distance decay (mild)
        float di = (dist_map[ni] >= 0) ?
            1.0f / (1.0f + 0.5f * static_cast<float>(dist_map[ni])) : 0.5f;

        // Scale diagonal
        prod.qubo.Q[vi][vi] *= di;

        // Degree bias (mild)
        int deg = graph.degree(ni);
        prod.qubo.Q[vi][vi] += -base * 0.05f * static_cast<float>(deg);

        // Scale off-diagonal
        for (int vj = vi + 1; vj < nv; vj++) {
            int nj = prod.var_to_node[vj];
            float dj = (dist_map[nj] >= 0) ?
                1.0f / (1.0f + 0.5f * static_cast<float>(dist_map[nj])) : 0.5f;
            float scale = (di + dj) / 2.0f;
            prod.qubo.Q[vi][vj] *= scale;
            prod.qubo.Q[vj][vi] *= scale;
        }
    }

    return prod;
}

// ── Factory-biased ──────────────────────────────────────────

QUBOEconomyInstance qubo_objective_factory_biased(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config,
    float factory_bias_strength) {

    // Start from production objective, add extra factory bias
    auto result = qubo_objective_production(graph, nodes, player_id, config);
    float base = static_cast<float>(config.factory_troops_per_tick);

    // Positive diagonal = prefer x_i = +1 = factory.
    // Scale relative to base production rate so the bias is meaningful.
    for (int vi = 0; vi < result.qubo.n; vi++) {
        result.qubo.Q[vi][vi] += base * factory_bias_strength;
    }

    return result;
}

// ── Economy solver integration ───────────────────────────────

#include "systems/economy/economy_solvers.hpp"

BuildPlan economy_solver_qubo(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config,
    const std::string& objective_name) {

    if (graph.num_nodes() == 0) return BuildPlan{};

    auto builder = get_qubo_objective(objective_name);
    if (!builder) return BuildPlan{};

    auto eco_inst = builder(graph, nodes, player_id, config);
    if (eco_inst.qubo.n == 0) return BuildPlan{};

    // Solve via SA (warm-started from greedy)
    auto solution = qubo_solve_sa(eco_inst.qubo, 42, 5000, 5.0f);

    // Map partition to BuildPlan: +1 → FACTORY, -1 → POWERPLANT
    BuildPlan plan;
    std::vector<NodeData> work = nodes;
    for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
        int node_idx = eco_inst.var_to_node[vi];
        NodeState target = (solution.partition[vi] > 0)
            ? NodeState::FACTORY : NodeState::POWERPLANT;
        plan.steps.push_back({node_idx, target});
        work[node_idx].state = target;
    }

    plan.estimated_production = compute_production_rate(graph, work, player_id, config);
    return plan;
}

// ── Registry ─────────────────────────────────────────────────

QObjectiveBuilder get_qubo_objective(const std::string& name) {
    if (name == "production") return qubo_objective_production;
    if (name == "adjacency") return qubo_objective_adjacency;
    if (name == "distance") return qubo_objective_distance;
    if (name == "degree") return qubo_objective_degree;
    if (name == "composite") return qubo_objective_composite;
    if (name == "factory_biased") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return qubo_objective_factory_biased(g, n, pid, c, 2.5f);
        };
    }
    return nullptr;
}

std::vector<std::string> list_qubo_objectives() {
    return {"production", "adjacency", "distance", "degree", "composite", "factory_biased"};
}
