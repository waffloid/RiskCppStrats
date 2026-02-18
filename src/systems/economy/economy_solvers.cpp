#include "systems/economy/economy_solvers.hpp"

#include <algorithm>
#include <cmath>
#include <random>
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
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config) {
    return economy_solver_mcmc_traced(graph, nodes, player_id, config, 2000, 5.0f, nullptr);
}

BuildPlan economy_solver_mcmc_traced(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config,
    int iterations,
    float initial_temp,
    MCMCTrace* trace) {

    int n = graph.num_nodes();
    if (n == 0) return BuildPlan{};

    // Find owned non-capital nodes (candidates for building assignment)
    std::vector<int> candidates;
    for (int i = 0; i < n; i++) {
        if (nodes[i].owner == player_id && nodes[i].state != NodeState::CAPITAL) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) return BuildPlan{};

    // Working copy of node data for assignment manipulation
    std::vector<NodeData> work = nodes;

    // Initialize: start from greedy solution for a warm start
    BuildPlan greedy = economy_solver_greedy(graph, nodes, player_id, config);
    for (const auto& step : greedy.steps) {
        if (step.node_idx >= 0 && step.node_idx < n) {
            work[step.node_idx].state = step.structure;
        }
    }

    float current_prod = compute_production_rate(graph, work, player_id, config);
    float best_prod = current_prod;
    std::vector<NodeData> best_state = work;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> node_dist(0, static_cast<int>(candidates.size()) - 1);
    std::uniform_real_distribution<float> accept_dist(0.0f, 1.0f);

    // Possible building states for candidates
    const NodeState options[] = {NodeState::DEFAULT, NodeState::FACTORY, NodeState::POWERPLANT};
    std::uniform_int_distribution<int> state_dist(0, 2);

    if (trace) {
        trace->initial_production = current_prod;
        trace->iterations = iterations;
        trace->accepted = 0;
        trace->improvements = 0;
    }

    for (int iter = 0; iter < iterations; iter++) {
        // Temperature schedule: linear cooling
        float temp = initial_temp * (1.0f - static_cast<float>(iter) / static_cast<float>(iterations));
        temp = std::max(temp, 0.01f);

        // Pick a random candidate node and a random new state
        int ci = node_dist(rng);
        int node_idx = candidates[ci];
        NodeState old_state = work[node_idx].state;
        NodeState new_state = options[state_dist(rng)];
        if (new_state == old_state) {
            // Still record trace for this iteration
            if (trace) {
                trace->production_per_iter.push_back(current_prod);
                trace->best_production_per_iter.push_back(best_prod);
            }
            continue;
        }

        // Apply change and measure
        work[node_idx].state = new_state;
        float new_prod = compute_production_rate(graph, work, player_id, config);

        float delta = new_prod - current_prod;
        bool accept = false;
        if (delta > 0.0f) {
            accept = true;
            if (trace) trace->improvements++;
        } else if (temp > 0.01f) {
            float prob = std::exp(delta / temp);
            accept = accept_dist(rng) < prob;
        }

        if (accept) {
            current_prod = new_prod;
            if (trace) trace->accepted++;
            if (current_prod > best_prod) {
                best_prod = current_prod;
                best_state = work;
            }
        } else {
            work[node_idx].state = old_state;  // revert
        }

        if (trace) {
            trace->production_per_iter.push_back(current_prod);
            trace->best_production_per_iter.push_back(best_prod);
        }
    }

    if (trace) trace->final_production = best_prod;

    // Extract BuildPlan from best state
    BuildPlan plan;
    for (int i = 0; i < n; i++) {
        if (best_state[i].owner != player_id) continue;
        if (best_state[i].state == NodeState::FACTORY || best_state[i].state == NodeState::POWERPLANT) {
            plan.steps.push_back({i, best_state[i].state});
        }
    }
    plan.estimated_production = best_prod;
    return plan;
}

BuildPlan economy_solver_branch_bound(
    const Graph& /*graph*/,
    const std::vector<NodeData>& /*nodes*/,
    int /*player_id*/,
    const GameConfig& /*config*/) {
    // TODO: Exact QP solve for small graphs.
    return BuildPlan{};
}

// ── Incremental MCMC stepper ──────────────────────────────────

MCMCStepper::MCMCStepper(const Graph& graph,
                         const std::vector<NodeData>& nodes,
                         int player_id,
                         const GameConfig& config,
                         int total_iterations,
                         float initial_temp)
    : graph_(&graph), player_id_(player_id), config_(config),
      total_iters_(total_iterations), initial_temp_(initial_temp),
      initial_nodes_(nodes) {

    int n = graph.num_nodes();
    for (int i = 0; i < n; i++) {
        if (nodes[i].owner == player_id && nodes[i].state != NodeState::CAPITAL) {
            candidates_.push_back(i);
        }
    }
    init_warm_start();
}

void MCMCStepper::init_warm_start() {
    work_ = initial_nodes_;
    BuildPlan greedy = economy_solver_greedy(*graph_, initial_nodes_, player_id_, config_);
    for (const auto& step : greedy.steps) {
        if (step.node_idx >= 0 && step.node_idx < graph_->num_nodes()) {
            work_[step.node_idx].state = step.structure;
        }
    }
    current_prod_ = compute_production_rate(*graph_, work_, player_id_, config_);
    best_prod_ = current_prod_;
    best_state_ = work_;
    iter_ = 0;
    rng_.seed(42);
    trace_ = MCMCTrace{};
    trace_.initial_production = current_prod_;
    trace_.iterations = total_iters_;
}

void MCMCStepper::reset(int total_iterations, float initial_temp) {
    total_iters_ = total_iterations;
    initial_temp_ = initial_temp;
    init_warm_start();
}

bool MCMCStepper::step(int n_iters) {
    if (candidates_.empty()) return false;

    std::uniform_int_distribution<int> node_dist(0, static_cast<int>(candidates_.size()) - 1);
    std::uniform_real_distribution<float> accept_dist(0.0f, 1.0f);
    const NodeState options[] = {NodeState::DEFAULT, NodeState::FACTORY, NodeState::POWERPLANT};
    std::uniform_int_distribution<int> state_dist(0, 2);

    int end = iter_ + n_iters;
    for (; iter_ < end; iter_++) {
        // Exponential cooling: temp = initial * decay^iter, asymptotes to min_temp
        float temp = initial_temp_ * std::exp(-3.0f * static_cast<float>(iter_) / static_cast<float>(std::max(1, total_iters_)));
        temp = std::max(temp, 0.01f);

        int ci = node_dist(rng_);
        int node_idx = candidates_[ci];
        NodeState old_state = work_[node_idx].state;
        NodeState new_state = options[state_dist(rng_)];
        if (new_state == old_state) {
            trace_.production_per_iter.push_back(current_prod_);
            trace_.best_production_per_iter.push_back(best_prod_);
            continue;
        }

        work_[node_idx].state = new_state;
        float new_prod = compute_production_rate(*graph_, work_, player_id_, config_);

        float delta = new_prod - current_prod_;
        bool accept = false;
        if (delta > 0.0f) {
            accept = true;
            trace_.improvements++;
        } else if (temp > 0.01f) {
            float prob = std::exp(delta / temp);
            accept = accept_dist(rng_) < prob;
        }

        if (accept) {
            current_prod_ = new_prod;
            trace_.accepted++;
            if (current_prod_ > best_prod_) {
                best_prod_ = current_prod_;
                best_state_ = work_;
            }
        } else {
            work_[node_idx].state = old_state;
        }

        trace_.production_per_iter.push_back(current_prod_);
        trace_.best_production_per_iter.push_back(best_prod_);
    }

    trace_.final_production = best_prod_;
    return true;
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

        // Each adjacent powerplant adds its bonus (stacks)
        int pp_count = 0;
        for (int nbr : graph.neighbors(i)) {
            if (nodes[nbr].state == NodeState::POWERPLANT && nodes[nbr].owner == player_id) {
                pp_count++;
            }
        }

        production += base + static_cast<float>(pp_count * config.powerplant_bonus);
    }

    return production;
}

float compute_theoretical_max_production(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config) {

    // Relaxed upper bound: assume every non-capital owned node is a factory
    // AND every producer has all its neighbors as powerplants. This is
    // infeasible (powerplants take factory slots) but provides a valid
    // upper bound. The exact optimum is NP-hard (MAX CUT variant).
    float max_prod = 0.0f;

    for (int i = 0; i < graph.num_nodes(); i++) {
        if (nodes[i].owner != player_id) continue;

        float base = 0.0f;
        if (nodes[i].state == NodeState::CAPITAL) {
            base = static_cast<float>(config.capital_troops_per_tick);
        } else {
            base = static_cast<float>(config.factory_troops_per_tick);
        }

        // Upper bound: every neighbor could be a PP
        int max_pp = graph.degree(i);
        max_prod += base + static_cast<float>(max_pp * config.powerplant_bonus);
    }

    return max_prod;
}
