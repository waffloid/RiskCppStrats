#ifndef CRISKY_COMBAT_TICK_RECORD_HPP
#define CRISKY_COMBAT_TICK_RECORD_HPP

#include <vector>

// Per-tick combat data for one player.
struct PlayerTickData {
    int troops_total = 0;       // on-node + in-transit
    int troops_on_nodes = 0;
    int troops_in_transit = 0;
    int nodes_owned = 0;
    int kills_this_tick = 0;
    int deaths_this_tick = 0;
    int cumulative_kills = 0;
    int cumulative_deaths = 0;
    float kd_ratio = 0.0f;
};

// Per-tick snapshot of the entire combat gym state.
struct CombatTickRecord {
    int tick = 0;
    std::vector<PlayerTickData> players;
};

#endif
