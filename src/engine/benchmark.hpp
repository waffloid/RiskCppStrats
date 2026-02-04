#ifndef CRISKY_BENCHMARK_HPP
#define CRISKY_BENCHMARK_HPP

#include <functional>
#include <vector>

#include "engine/game.hpp"
#include "engine/graph.hpp"
#include "engine/game_config.hpp"
#include "player/player_interface.hpp"

// Per-node state override for benchmark scenarios.
struct NodeOverride {
    int node_idx;
    NodeState state;
    int owner;      // -1 = unowned
    int troops;
};

struct ScenarioResult {
    int nodes_captured;     // nodes owned by test_player at end
    int total_troops;       // total troops of test_player (nodes + travelling)
    int ticks_elapsed;
    bool won;               // test_player is sole real-player survivor
};

// Run a benchmark scenario on a pre-built graph.
// - test_player_id: the player whose AI is under test
// - opponent_ai: used for all other real players
// - The game is constructed with the given config, graph, and capitals.
//   Then node overrides are applied (set_node_state for each override).
// - Simulation runs up to max_ticks or until game_over.
ScenarioResult run_scenario(
    const GameConfig& config,
    Graph graph,
    const std::vector<int>& capitals,
    const std::vector<NodeOverride>& overrides,
    PlayerInterface& test_ai,
    int test_player_id,
    PlayerInterface& opponent_ai,
    int max_ticks,
    float dt = 1.0f);

#endif
