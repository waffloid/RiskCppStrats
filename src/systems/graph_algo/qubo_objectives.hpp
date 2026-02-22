#ifndef CRISKY_QUBO_OBJECTIVES_HPP
#define CRISKY_QUBO_OBJECTIVES_HPP

#include <functional>
#include <string>
#include <vector>

#include "engine/game_config.hpp"
#include "engine/game_state.hpp"
#include "engine/graph.hpp"
#include "systems/graph_algo/maxcut_solvers.hpp"

// ──────────────────────────────────────────────────────────────
//  Q matrix builders for the economy QUBO.
//
//  Each builder takes game state and produces a QUBOInstance whose
//  variables correspond to owned non-capital nodes.  The mapping
//  between variable indices and node indices is returned alongside.
//
//  Convention: x_i = +1 → FACTORY,  x_i = -1 → POWERPLANT
// ──────────────────────────────────────────────────────────────

struct QUBOEconomyInstance {
    QUBOInstance qubo;
    std::vector<int> var_to_node;  // variable index → node index in graph
};

using QObjectiveBuilder = std::function<QUBOEconomyInstance(
    const Graph&, const std::vector<NodeData>&, int player_id, const GameConfig&)>;

QObjectiveBuilder get_qubo_objective(const std::string& name);
std::vector<std::string> list_qubo_objectives();

// ── Named builders ───────────────────────────────────────────

// Faithful production objective.
// Q[i][j] = factory_rate * powerplant_bonus / 4  for adjacent owned nodes.
// Q[i][i] = factory_rate / 2  (linear preference: factories produce, powerplants don't).
QUBOEconomyInstance qubo_objective_production(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Pure max-cut (adjacency only, no linear terms).
// Q[i][j] = 1.0 for adjacent owned nodes, Q[i][i] = 0.
QUBOEconomyInstance qubo_objective_adjacency(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Distance-weighted: coupling decays with BFS distance from capital.
QUBOEconomyInstance qubo_objective_distance(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Degree-weighted: high-degree nodes biased toward powerplant.
QUBOEconomyInstance qubo_objective_degree(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Composite: weighted combination of production + degree biases.
QUBOEconomyInstance qubo_objective_composite(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Factory-biased: production objective + extra positive linear bias
// to push toward ~70% factories. factory_bias_strength controls how
// much extra preference for factories (units of base production rate).
QUBOEconomyInstance qubo_objective_factory_biased(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config,
    float factory_bias_strength = 1.5f);

// ── Economy solver integration ───────────────────────────────

#include "systems/common/types.hpp"

// QUBO-based economy solver: builds Q matrix, solves via SA,
// maps partition to BuildPlan (factory/powerplant assignment).
BuildPlan economy_solver_qubo(
    const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config,
    const std::string& objective_name = "production",
    const std::string& solver_method = "sa");

#endif
