#ifndef CRISKY_COMBAT_GYM_HPP
#define CRISKY_COMBAT_GYM_HPP

#include "gyms/combat_benchmarks.hpp"
#include "gyms/combat_tick_record.hpp"
#include "engine/player_interface.hpp"

#include <string>
#include <vector>

class Game;

struct CombatGymResult {
    std::string benchmark;
    std::string solver;
    std::string opponent;
    float spartan_multiplier = 1.0f;

    int ticks_elapsed = 0;
    int winner = -1;  // 0 = solver wins, 1 = opponent wins, -1 = draw/timeout

    // Per-player end-of-match stats
    int nodes_p0 = 0;
    int nodes_p1 = 0;
    int troops_p0 = 0;
    int troops_p1 = 0;

    // Accumulated deaths suffered (summed over all ticks)
    int deaths_p0 = 0;
    int deaths_p1 = 0;

    // Per-tick data (populated when collect_ticks=true)
    std::vector<CombatTickRecord> tick_records;
};

// Per-tick metrics from a combat simulation step — shared between headless and viz.
struct CombatTickMetrics {
    std::vector<int> troops;        // total per real player (on-node + in-transit)
    std::vector<float> territory;   // fractional node ownership per real player
    std::vector<int> deaths;        // deaths this tick per real player
};

// Advance one tick of combat simulation. Calls decide for each player,
// clears builds (pure combat), runs game.tick, and extracts per-player metrics.
// players vector must be indexed by player id, size >= n_total.
CombatTickMetrics combat_gym_tick(
    Game& game,
    const std::vector<PlayerInterface*>& players,
    float dt = 1.0f);

// Run a single combat gym match.
// benchmark:  map layout, initial state, tick limit
// solver_name:   model name for player 0 (test subject)
// opponent_name: model name for player 1
// spartan_multiplier: scale opponent's initial troops (>1 = harder)
// collect_ticks: if true, populate result.tick_records with per-tick data
CombatGymResult run_combat_gym(
    const CombatBenchmark& benchmark,
    const std::string& solver_name,
    const std::string& opponent_name,
    float spartan_multiplier = 1.0f,
    bool collect_ticks = false);

#endif
