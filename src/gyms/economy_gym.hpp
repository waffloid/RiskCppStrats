#ifndef CRISKY_ECONOMY_GYM_HPP
#define CRISKY_ECONOMY_GYM_HPP

#include <string>
#include <vector>

#include "engine/game_config.hpp"
#include "engine/game_state.hpp"
#include "engine/graph.hpp"
#include "systems/common/types.hpp"
#include "systems/economy/economy_solvers.hpp"

// State of the economy gym.  Can be constructed standalone (synthetic graph)
// or projected from a live Game.
struct EconomyGymState {
    Graph graph;
    std::vector<NodeData> nodes;
    int player_id = 0;
    GameConfig config{};

    // Solver result
    BuildPlan plan;
    float production_rate = 0.0f;
    float theoretical_max = 0.0f;
    float efficiency = 0.0f;

    // Graph metadata
    int n_factories = 0;
    int n_powerplants = 0;
};

// Run the economy gym: generate a graph, assign ownership, run a solver,
// measure production.  Returns the resulting state.
//
// - solver_name: "greedy", "bootstrap", "mcmc", "branch_bound"
// - seed: random seed for graph generation
// - n_nodes_hint: approximate number of nodes (Poisson intensity)
EconomyGymState run_economy_gym(
    const std::string& solver_name,
    uint64_t seed,
    int n_nodes_hint,
    const GameConfig& config);

// Get a named solver by string.  Returns nullptr if unknown.
EconomySolver get_economy_solver(const std::string& name);

#endif
