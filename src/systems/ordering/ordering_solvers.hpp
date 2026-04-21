#ifndef CRISKY_ORDERING_SOLVERS_HPP
#define CRISKY_ORDERING_SOLVERS_HPP

#include "systems/common/types.hpp"
#include "engine/graph.hpp"
#include "engine/game_state.hpp"
#include "engine/game_config.hpp"

#include <functional>
#include <string>
#include <vector>

// Returns cost in troops for building a given structure.
inline int building_cost(NodeState state, const GameConfig& config) {
    switch (state) {
        case NodeState::FACTORY:    return config.cost_factory;
        case NodeState::POWERPLANT: return config.cost_powerplant;
        case NodeState::FORT:       return config.cost_fort;
        case NodeState::ARTILLERY:  return config.cost_artillery;
        default: return 0;
    }
}

// An ordering solver returns a permutation of plan step indices.
// order[0] is the index of the first step to execute, etc.
using OrderingSolver = std::function<std::vector<int>(
    const BuildPlan&, const Graph&, const std::vector<NodeData>&,
    int player_id, const GameConfig&)>;

// Execute steps in the order they appear in the plan.
std::vector<int> ordering_solver_sequential(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Cheapest structures first (factory < fort < powerplant < artillery).
std::vector<int> ordering_solver_cheapest_first(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Structures closest to capital first (BFS distance).
std::vector<int> ordering_solver_nearest_first(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Greedy: at each step, pick the build giving the largest production increase.
std::vector<int> ordering_solver_production_gradient(
    const BuildPlan& plan, const Graph& graph, const std::vector<NodeData>& nodes,
    int player_id, const GameConfig& config);

// Get a named ordering solver. Returns nullptr if not found.
OrderingSolver get_ordering_solver(const std::string& name);

// List available ordering solver names.
std::vector<std::string> list_ordering_solvers();

#endif
