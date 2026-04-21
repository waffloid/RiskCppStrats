#ifndef CRISKY_METRICS_WRITER_HPP
#define CRISKY_METRICS_WRITER_HPP

#include "observability/ai_metrics.hpp"
#include <cstdio>
#include <cmath>
#include <vector>

// Writes per-tick metrics as JSON lines to a file, and accumulates
// running statistics for an end-of-game summary.
class MetricsWriter {
public:
    // Open file for writing. Pass nullptr to disable file output.
    explicit MetricsWriter(const char* filepath, int n_players);
    ~MetricsWriter();

    MetricsWriter(const MetricsWriter&) = delete;
    MetricsWriter& operator=(const MetricsWriter&) = delete;

    // Write one tick's metrics for all players.
    void write_tick(int tick,
                    const std::vector<const AIMetricsSnapshot*>& ai_snapshots,
                    const std::vector<GameMetricsSnapshot>& game_snapshots);

    // Print summary statistics to stdout at game end.
    void print_summary() const;

private:
    FILE* file_ = nullptr;
    int n_players_;

    // Welford's online algorithm for running mean/stdev/min/max
    struct RunningStats {
        double sum = 0, sum_sq = 0;
        double min_val = 1e30, max_val = -1e30;
        int count = 0;

        void add(double v) {
            sum += v; sum_sq += v * v; count++;
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
        double mean() const { return count > 0 ? sum / count : 0; }
        double stdev() const {
            if (count < 2) return 0;
            double variance = (sum_sq - sum * sum / count) / (count - 1);
            return std::sqrt(std::max(variance, 0.0));
        }
    };

    struct PlayerStats {
        RunningStats knapsack_ratio;
        RunningStats v2_efficiency;
        RunningStats distribution_entropy;
        RunningStats economy_efficiency;
        RunningStats territory_fraction;
        RunningStats perimeter_ratio;
        RunningStats kd_ratio;
    };
    std::vector<PlayerStats> player_stats_;

    // Format one tick as a JSON line and write to file_.
    void write_json_line(int tick,
                         const std::vector<const AIMetricsSnapshot*>& ai,
                         const std::vector<GameMetricsSnapshot>& game);
};

#endif
