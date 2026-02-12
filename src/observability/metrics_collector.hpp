#ifndef CRISKY_METRICS_COLLECTOR_HPP
#define CRISKY_METRICS_COLLECTOR_HPP

#include "observability/ai_metrics.hpp"
#include <vector>

class Game;

// External observer that computes per-player GameMetricsSnapshot
// from const Game& each tick. Does not modify game state.
class MetricsCollector {
public:
    explicit MetricsCollector(int n_players);

    // Call once per tick, after game.tick(). Populates snapshots for all players.
    void collect(const Game& game, int tick);

    const GameMetricsSnapshot& snapshot(int player_id) const { return snapshots_[player_id]; }

private:
    int n_players_;
    std::vector<GameMetricsSnapshot> snapshots_;
    std::vector<int> prev_nodes_owned_;     // for expansion rate
    std::vector<int> cumulative_kills_;     // running totals
    std::vector<int> cumulative_deaths_;
};

#endif
