#ifndef CRISKY_TRANSPORT_GYM_HPP
#define CRISKY_TRANSPORT_GYM_HPP

#include "engine/graph.hpp"
#include "engine/game_config.hpp"

#include <optional>
#include <string>
#include <vector>

// Named transport scenario: graph + initial/target troop distributions.
struct TransportPreset {
    std::string name;
    Graph graph;
    std::vector<int> initial_troops;  // per-node starting troops
    std::vector<int> target_troops;   // per-node desired distribution
};

std::optional<TransportPreset> get_transport_preset(const std::string& name);
std::vector<std::string> list_transport_presets();

// Per-tick data for convergence analysis.
struct TransportTickData {
    float loss;
    float loss_delta;       // change from previous tick
    int troops_in_transit;
};

// Result of a transport gym run.
struct TransportGymResult {
    int total_ticks = 0;
    float initial_loss = 0.0f;
    float final_loss = 0.0f;
    int ticks_to_converge = 0;  // first tick where loss < threshold (0 = didn't)

    std::vector<TransportTickData> ticks;
};

// Run a transport gym simulation.
// Single-player game with zero production/combat. Measures how quickly
// the transport solver redistributes troops from initial to target.
TransportGymResult run_transport_gym(
    const TransportPreset& preset,
    const std::string& solver_name,
    const std::string& loss_name,
    int max_ticks);

#endif
