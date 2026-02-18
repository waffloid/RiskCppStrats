#ifndef CRISKY_ECONOMY_SOLVERS_HPP
#define CRISKY_ECONOMY_SOLVERS_HPP

#include <functional>
#include <vector>

#include "engine/game_config.hpp"
#include "engine/game_state.hpp"
#include "engine/graph.hpp"
#include "systems/common/types.hpp"

// ──────────────────────────────────────────────────────────────
//  Economy solvers: pure functions that decide factory/powerplant
//  placement.  These are the extracted core logic from the economy
//  sub-agents, usable both in the real AI and in the economy gym.
// ──────────────────────────────────────────────────────────────

// Solver type: given a graph snapshot, produce a build plan.
using EconomySolver = std::function<BuildPlan(
    const Graph&,
    const std::vector<NodeData>&,
    int player_id,
    const GameConfig&
)>;

// ── Greedy solver ──────────────────────────────────────────────
// For each owned node, decide factory vs powerplant based on neighbor
// balance.  Returns only immediately-buildable commands (troops > cost).
// Extracted from EconomySubAgent::contribute() build-command logic.
BuildPlan economy_solver_greedy(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// ── Bootstrap solver ───────────────────────────────────────────
// BFS from capital: plan 4 nearest factories + 1 best-connected
// powerplant.  Returns a full multi-step build plan.
// Extracted from BootstrapEconomySubAgent::generate_plan().
//
// num_factories: how many factories to plan (default 4).
BuildPlan economy_solver_bootstrap(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config,
    int num_factories = 4);

// ── Stubs for future solvers ───────────────────────────────────

// Per-iteration trace for MCMC solver (for visualization).
struct MCMCTrace {
    std::vector<float> production_per_iter;      // production at each iteration
    std::vector<float> best_production_per_iter; // best seen so far at each iteration
    float initial_production = 0.0f;
    float final_production = 0.0f;
    int iterations = 0;
    int accepted = 0;        // total accepted moves
    int improvements = 0;    // strictly improving moves
};

// MCMC (simulated annealing) over factory/powerplant assignments.
// Maximizes production rate by random swaps with Metropolis acceptance.
BuildPlan economy_solver_mcmc(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// MCMC with trace output for visualization.
BuildPlan economy_solver_mcmc_traced(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config,
    int iterations,
    float initial_temp,
    MCMCTrace* trace);

// Exact branch-and-bound for the QP.  Feasible for small graphs only.
// Not yet implemented — returns an empty plan.
BuildPlan economy_solver_branch_bound(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// ── Incremental MCMC stepper (for live visualization) ─────────

#include <random>

class MCMCStepper {
public:
    MCMCStepper(const Graph& graph,
                const std::vector<NodeData>& nodes,
                int player_id,
                const GameConfig& config,
                int total_iterations,
                float initial_temp);

    // Advance by n iterations. Always returns true (runs indefinitely).
    bool step(int n_iters);

    // Reset to initial state (greedy warm start) with new params.
    void reset(int total_iterations, float initial_temp);

    // Accessors
    int iteration() const { return iter_; }
    float current_production() const { return current_prod_; }
    float best_production() const { return best_prod_; }
    float& initial_temp() { return initial_temp_; }
    int& cooling_rate() { return total_iters_; }
    const std::vector<NodeData>& current_state() const { return work_; }
    const std::vector<NodeData>& best_state() const { return best_state_; }
    MCMCTrace& trace() { return trace_; }
    const MCMCTrace& trace() const { return trace_; }

private:
    const Graph* graph_;
    int player_id_;
    GameConfig config_;
    int total_iters_;
    float initial_temp_;

    std::vector<int> candidates_;
    std::vector<NodeData> initial_nodes_;
    std::vector<NodeData> work_;
    std::vector<NodeData> best_state_;
    float current_prod_ = 0.0f;
    float best_prod_ = 0.0f;
    int iter_ = 0;

    std::mt19937 rng_{42};
    MCMCTrace trace_;

    void init_warm_start();
};

// ── Utility ────────────────────────────────────────────────────

// Compute the production rate for a given assignment of nodes.
// Counts factories, capitals, and powerplant bonuses.
float compute_production_rate(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// Compute a relaxed upper bound on production for owned nodes.
// Assumes every non-capital owned node is a factory AND every producer
// is powered. This is infeasible (powerplants take factory slots) but
// provides a valid upper bound. The exact optimum is NP-hard.
float compute_theoretical_max_production(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

#endif
