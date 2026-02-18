#ifndef CRISKY_MAXCUT_SOLVERS_HPP
#define CRISKY_MAXCUT_SOLVERS_HPP

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "engine/graph.hpp"

// ──────────────────────────────────────────────────────────────
//  QUBO / Max-Cut solver types and implementations.
//
//  Solves: max x^T Q x  where x_i ∈ {-1, +1}.
//  Standard max-cut is the special case Q = adjacency matrix.
//  General QUBO adds linear terms on the diagonal: Q[i][i] = h_i.
//
//  For the economy problem:
//    +1 = factory,  -1 = powerplant
//    Q[i][j] encodes adjacency bonus (factory next to powerplant)
//    Q[i][i] encodes linear preference (factories produce)
// ──────────────────────────────────────────────────────────────

struct QUBOInstance {
    int n = 0;
    std::vector<std::vector<float>> Q;  // n×n symmetric
    const Graph* graph = nullptr;       // optional, for topology-aware ops

    // Build from a Graph with optional per-edge weights.
    // Standard max-cut: Q[i][j] = w_ij for edges, diag = 0.
    static QUBOInstance from_graph(const Graph& graph,
                                   const std::vector<float>& edge_weights = {});
};

struct QUBOSolution {
    std::vector<int> partition;    // +1 or -1 per variable
    float objective = 0.0f;        // x^T Q x
    std::vector<float> flip_gain;  // per-node improvement if flipped
    int iterations = 0;
    float time_ms = 0.0f;
    std::string solver_name;
};

struct QUBOTrace {
    std::vector<float> objective_per_iter;
    std::vector<float> best_objective_per_iter;
    float initial_objective = 0.0f;
    float final_objective = 0.0f;
    int iterations = 0;
    int accepted = 0;
    int improvements = 0;
};

// ── Solver type + registry ───────────────────────────────────

using QUBOSolver = std::function<QUBOSolution(
    const QUBOInstance& instance, uint64_t seed)>;

QUBOSolver get_qubo_solver(const std::string& name);
std::vector<std::string> list_qubo_solvers();

// ── Individual solvers ───────────────────────────────────────

// Greedy local search: repeatedly flip nodes that increase objective.
QUBOSolution qubo_solve_greedy(const QUBOInstance& instance, uint64_t seed);

// Simulated annealing with greedy warm start.
QUBOSolution qubo_solve_sa(const QUBOInstance& instance, uint64_t seed,
                            int iterations = 10000, float initial_temp = 5.0f);

// SA with trace output for visualization.
QUBOSolution qubo_solve_sa_traced(const QUBOInstance& instance, uint64_t seed,
                                   int iterations, float initial_temp,
                                   QUBOTrace* trace);

// Goemans-Williamson SDP relaxation + hyperplane rounding (uses Eigen).
QUBOSolution qubo_solve_gw(const QUBOInstance& instance, uint64_t seed,
                            int sdp_iterations = 500, int rounding_trials = 50);

// ── Incremental SA stepper (for viz) ─────────────────────────

class QUBOStepper {
public:
    QUBOStepper(const QUBOInstance& instance, uint64_t seed,
                int total_iterations = 10000, float initial_temp = 5.0f);

    bool step(int n_iters);

    void reset(int total_iterations, float initial_temp);
    void reset_seed(uint64_t seed);

    int iteration() const { return iter_; }
    float current_objective() const { return current_obj_; }
    float best_objective() const { return best_obj_; }
    float& initial_temp() { return initial_temp_; }
    int& total_iters() { return total_iters_; }
    const std::vector<int>& current_partition() const { return current_; }
    const std::vector<int>& best_partition() const { return best_; }
    const std::vector<float>& flip_gains() const { return flip_gain_; }
    QUBOTrace& trace() { return trace_; }
    const QUBOTrace& trace() const { return trace_; }

private:
    const QUBOInstance* instance_;
    int total_iters_;
    float initial_temp_;

    std::vector<int> current_;
    std::vector<int> best_;
    std::vector<float> flip_gain_;
    float current_obj_ = 0.0f;
    float best_obj_ = 0.0f;
    int iter_ = 0;

    std::mt19937 rng_;
    QUBOTrace trace_;

    void init_greedy_warmstart();
    void update_flip_gains_after_flip(int node);
};

// ── Utilities ────────────────────────────────────────────────

float compute_qubo_objective(const QUBOInstance& instance,
                              const std::vector<int>& partition);

std::vector<float> compute_qubo_flip_gains(const QUBOInstance& instance,
                                            const std::vector<int>& partition);

#endif
