#ifndef CRISKY_ORDERING_GYM_HPP
#define CRISKY_ORDERING_GYM_HPP

#include "engine/game_config.hpp"

#include <string>
#include <vector>

// Result of an ordering gym run.
// Simulates accumulating production and spending on builds in a given order.
struct OrderingGymResult {
    std::vector<int> execution_order;         // permutation of plan step indices
    std::vector<int> step_completed_tick;     // tick at which each ordered step finishes
    std::vector<float> production_after_step; // production rate after each build

    int total_ticks = 0;                      // ticks to complete all builds
    float accumulated_production = 0.0f;      // integral of production rate over time
    int n_steps = 0;                          // number of build steps
};

// Run an ordering gym experiment.
// 1. Generates a Poisson graph via seed
// 2. Runs economy solver to get a BuildPlan
// 3. Runs ordering solver to determine execution sequence
// 4. Simulates production accumulation to determine when each build completes
//
// economy_solver_name: e.g. "greedy", "bootstrap"
// ordering_solver_name: e.g. "sequential", "cheapest_first", "production_gradient"
OrderingGymResult run_ordering_gym(
    const std::string& economy_solver_name,
    const std::string& ordering_solver_name,
    uint64_t seed,
    const GameConfig& config);

#endif
