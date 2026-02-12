#ifndef CRISKY_TRANSPORT_SOLVERS_HPP
#define CRISKY_TRANSPORT_SOLVERS_HPP

#include "engine/graph.hpp"
#include "engine/game_state.hpp"
#include "engine/player_interface.hpp"  // TroopCommand

#include <functional>
#include <string>
#include <vector>

// Greedy potential-driven transport solver.
// Extracted from DistributionAIPlayer::execute_transport.
//
// For each owned, unmasked node with enough troops:
//   - Compute positive potential differences to neighbors
//   - Sigmoid-weighted outflow proportional to potential strength
//   - Send troops toward neighbors with higher potential values
//
// potential[i] is any per-node "desirability" signal:
//   - In the AI: deficit = target_fraction * total - current
//   - In the transport gym: deficit = target - current
std::vector<TroopCommand> transport_solver_greedy(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    const std::vector<float>& potential,
    int player_id,
    const std::vector<bool>& masked,
    float outflow_rate = 0.1f,
    int min_troops = 5);

// Solver typedef for the transport gym (captures rate/min_troops via closure).
using TransportSolver = std::function<std::vector<TroopCommand>(
    const Graph&, const std::vector<NodeData>&,
    const std::vector<float>&, int, const std::vector<bool>&)>;

// Get a named transport solver with default parameters.
TransportSolver get_transport_solver(const std::string& name);

// List available solver names.
std::vector<std::string> list_transport_solvers();

#endif
