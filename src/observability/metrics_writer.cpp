#include "observability/metrics_writer.hpp"
#include <cstdio>
#include <cstring>

MetricsWriter::MetricsWriter(const char* filepath, int n_players)
    : n_players_(n_players), player_stats_(n_players) {
    if (filepath && std::strlen(filepath) > 0) {
        file_ = std::fopen(filepath, "w");
    }
}

MetricsWriter::~MetricsWriter() {
    if (file_) std::fclose(file_);
}

void MetricsWriter::write_tick(int tick,
                                const std::vector<const AIMetricsSnapshot*>& ai_snapshots,
                                const std::vector<GameMetricsSnapshot>& game_snapshots) {
    // Accumulate running stats
    for (int p = 0; p < n_players_; p++) {
        auto& ps = player_stats_[p];
        if (p < static_cast<int>(ai_snapshots.size()) && ai_snapshots[p]) {
            ps.knapsack_ratio.add(ai_snapshots[p]->knapsack_ratio);
            ps.v2_efficiency.add(ai_snapshots[p]->v2_efficiency);
            ps.attention_entropy.add(ai_snapshots[p]->attention_entropy);
        }
        if (p < static_cast<int>(game_snapshots.size())) {
            ps.economy_efficiency.add(game_snapshots[p].economy_efficiency);
            ps.territory_fraction.add(game_snapshots[p].territory_fraction);
            ps.perimeter_ratio.add(game_snapshots[p].perimeter_ratio);
            ps.kd_ratio.add(game_snapshots[p].kd_ratio);
        }
    }

    // Write JSON line to file
    if (file_) {
        write_json_line(tick, ai_snapshots, game_snapshots);
    }
}

void MetricsWriter::write_json_line(int tick,
                                     const std::vector<const AIMetricsSnapshot*>& ai,
                                     const std::vector<GameMetricsSnapshot>& game) {
    std::fprintf(file_, "{\"tick\":%d,\"players\":[", tick);
    for (int p = 0; p < n_players_; p++) {
        if (p > 0) std::fprintf(file_, ",");
        std::fprintf(file_, "{\"id\":%d", p);

        // AI metrics
        if (p < static_cast<int>(ai.size()) && ai[p]) {
            std::fprintf(file_,
                ",\"knapsack_ratio\":%.4f,\"knapsack_n_targets\":%d"
                ",\"v2_efficiency\":%.4f,\"v2_targets_attacked\":%d"
                ",\"attention_entropy\":%.3f,\"attention_gradient_util\":%.3f",
                ai[p]->knapsack_ratio, ai[p]->knapsack_n_targets,
                ai[p]->v2_efficiency, ai[p]->v2_targets_attacked,
                ai[p]->attention_entropy, ai[p]->attention_gradient_util);
        }

        // Game metrics
        if (p < static_cast<int>(game.size())) {
            const auto& g = game[p];
            std::fprintf(file_,
                ",\"kills\":%d,\"deaths\":%d,\"kd_ratio\":%.2f"
                ",\"troops\":%d,\"production\":%d,\"economy_eff\":%.3f"
                ",\"nodes\":%d,\"territory\":%.3f,\"perimeter_ratio\":%.2f",
                g.cumulative_kills, g.cumulative_deaths, g.kd_ratio,
                g.total_troops, g.production_per_tick, g.economy_efficiency,
                g.nodes_owned, g.territory_fraction, g.perimeter_ratio);
        }

        std::fprintf(file_, "}");
    }
    std::fprintf(file_, "]}\n");
}

void MetricsWriter::print_summary() const {
    std::printf("\n=== AI Metrics Summary ===\n");
    for (int p = 0; p < n_players_; p++) {
        const auto& ps = player_stats_[p];
        std::printf("\nPlayer %d:\n", p);
        std::printf("  Knapsack ratio     : mean=%.3f  stdev=%.3f  min=%.3f  max=%.3f\n",
                    ps.knapsack_ratio.mean(), ps.knapsack_ratio.stdev(),
                    ps.knapsack_ratio.min_val, ps.knapsack_ratio.max_val);
        std::printf("  V2 efficiency      : mean=%.4f  stdev=%.4f  min=%.4f  max=%.4f\n",
                    ps.v2_efficiency.mean(), ps.v2_efficiency.stdev(),
                    ps.v2_efficiency.min_val, ps.v2_efficiency.max_val);
        std::printf("  Attention entropy  : mean=%.2f  stdev=%.2f  min=%.2f  max=%.2f\n",
                    ps.attention_entropy.mean(), ps.attention_entropy.stdev(),
                    ps.attention_entropy.min_val, ps.attention_entropy.max_val);
        std::printf("  Economy efficiency : mean=%.3f  stdev=%.3f  min=%.3f  max=%.3f\n",
                    ps.economy_efficiency.mean(), ps.economy_efficiency.stdev(),
                    ps.economy_efficiency.min_val, ps.economy_efficiency.max_val);
        std::printf("  Territory fraction : mean=%.3f  stdev=%.3f  min=%.3f  max=%.3f\n",
                    ps.territory_fraction.mean(), ps.territory_fraction.stdev(),
                    ps.territory_fraction.min_val, ps.territory_fraction.max_val);
        std::printf("  Perimeter ratio    : mean=%.2f  stdev=%.2f  min=%.2f  max=%.2f\n",
                    ps.perimeter_ratio.mean(), ps.perimeter_ratio.stdev(),
                    ps.perimeter_ratio.min_val, ps.perimeter_ratio.max_val);
        std::printf("  K/D ratio          : mean=%.2f  stdev=%.2f  min=%.2f  max=%.2f\n",
                    ps.kd_ratio.mean(), ps.kd_ratio.stdev(),
                    ps.kd_ratio.min_val, ps.kd_ratio.max_val);
    }
    std::printf("\n");
}
