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

// MCMC over factory/powerplant assignments.  Maximizes production rate.
// Not yet implemented — returns an empty plan.
BuildPlan economy_solver_mcmc(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// Exact branch-and-bound for the QP.  Feasible for small graphs only.
// Not yet implemented — returns an empty plan.
BuildPlan economy_solver_branch_bound(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// ── Utility ────────────────────────────────────────────────────

// Compute the production rate for a given assignment of nodes.
// Counts factories, capitals, and powerplant bonuses.
float compute_production_rate(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

// Compute theoretical max production if every owned factory/capital
// were powered by an adjacent powerplant.
float compute_theoretical_max_production(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    int player_id,
    const GameConfig& config);

#endif
