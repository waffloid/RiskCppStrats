#include "observability/metrics_collector.hpp"
#include "engine/game.hpp"

#include <algorithm>

MetricsCollector::MetricsCollector(int n_players)
    : n_players_(n_players),
      snapshots_(n_players),
      prev_nodes_owned_(n_players, 0),
      cumulative_kills_(n_players, 0),
      cumulative_deaths_(n_players, 0) {}

void MetricsCollector::collect(const Game& game, int tick) {
    const auto& nodes = game.node_data();
    const auto& graph = game.graph();
    const auto& config = game.config();
    const auto& tick_deaths = game.tick_deaths();
    int n = graph.num_nodes();

    for (int p = 0; p < n_players_; p++) {
        auto& s = snapshots_[p];
        s.tick = tick;
        s.player_id = p;

        // Reset per-tick accumulators
        s.total_troops = 0;
        s.nodes_owned = 0;
        s.factories_owned = 0;
        s.factories_powered = 0;
        s.powerplants_owned = 0;
        s.production_per_tick = 0;
        s.theoretical_max_production = 0;
        s.frontier_perimeter = 0;

        for (int i = 0; i < n; i++) {
            const auto& nd = nodes[i];
            s.total_troops += nd.troops[p];

            if (nd.owner != p) continue;
            s.nodes_owned++;

            // Economy: count buildings and production
            switch (nd.state) {
                case NodeState::CAPITAL:
                    s.production_per_tick += config.capital_troops_per_tick;
                    s.theoretical_max_production += config.capital_troops_per_tick + config.powerplant_bonus;
                    // Check if powered by adjacent powerplant
                    for (int nbr : graph.nodes[i].neighbor_indices) {
                        if (nodes[nbr].state == NodeState::POWERPLANT && nodes[nbr].owner == p) {
                            s.production_per_tick += config.powerplant_bonus;
                            break;
                        }
                    }
                    break;

                case NodeState::FACTORY:
                    s.factories_owned++;
                    s.production_per_tick += config.factory_troops_per_tick;
                    s.theoretical_max_production += config.factory_troops_per_tick + config.powerplant_bonus;
                    for (int nbr : graph.nodes[i].neighbor_indices) {
                        if (nodes[nbr].state == NodeState::POWERPLANT && nodes[nbr].owner == p) {
                            s.production_per_tick += config.powerplant_bonus;
                            s.factories_powered++;
                            break;
                        }
                    }
                    break;

                case NodeState::POWERPLANT:
                    s.powerplants_owned++;
                    break;

                default:
                    break;
            }

            // Frontier perimeter: count edges to non-owned neighbors
            for (int nbr : graph.nodes[i].neighbor_indices) {
                if (nodes[nbr].owner != p) s.frontier_perimeter++;
            }
        }

        // Territory
        s.total_nodes = n;
        s.territory_fraction = static_cast<float>(s.nodes_owned) / static_cast<float>(n);
        s.perimeter_ratio = s.nodes_owned > 0
            ? static_cast<float>(s.frontier_perimeter) / static_cast<float>(s.nodes_owned)
            : 0.0f;
        s.expansion_rate = s.nodes_owned - prev_nodes_owned_[p];
        prev_nodes_owned_[p] = s.nodes_owned;

        // Economy efficiency
        s.economy_efficiency = s.theoretical_max_production > 0
            ? static_cast<float>(s.production_per_tick) / static_cast<float>(s.theoretical_max_production)
            : 0.0f;

        // Combat K/D from exact engine counters
        // tick_deaths[p] = deaths suffered by player p this tick.
        // Kills = sum of all other players' deaths (simplified attribution).
        s.deaths_this_tick = (p < static_cast<int>(tick_deaths.size())) ? tick_deaths[p] : 0;
        cumulative_deaths_[p] += s.deaths_this_tick;
        s.cumulative_deaths = cumulative_deaths_[p];

        // Attribute kills: player p's kills = sum of deaths of other players
        // (this is a simplification — in multi-player combat the attribution is shared)
        s.kills_this_tick = 0;
        for (int other = 0; other < n_players_; other++) {
            if (other == p) continue;
            if (other < static_cast<int>(tick_deaths.size())) {
                s.kills_this_tick += tick_deaths[other];
            }
        }
        cumulative_kills_[p] += s.kills_this_tick;
        s.cumulative_kills = cumulative_kills_[p];

        s.kd_ratio = s.cumulative_deaths > 0
            ? static_cast<float>(s.cumulative_kills) / static_cast<float>(s.cumulative_deaths)
            : 0.0f;
    }
}
