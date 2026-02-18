#include "gyms/combat_gym.hpp"
#include "gyms/combat_benchmarks.hpp"
#include "systems/combat/combat_solvers.hpp"
#include "ai/players/passive_player.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "engine/game.hpp"

#include "viz/viz_app.hpp"
#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/bar_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"
#include "viz/panels/tabbed_panel.hpp"

#include "viz/imgui_theme.hpp"

#include "raylib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>

static void print_usage() {
    std::printf("Usage: gym_combat_viz [options]\n");
    std::printf("  --solver=NAME      AI model for player 0 (default: v3)\n");
    std::printf("  --opponent=NAME    AI model for player 1 (default: v2_knapsack)\n");
    std::printf("  --benchmark=NAME   Benchmark map (default: corridor)\n");
    std::printf("  --spartan=X        Troop multiplier for opponent (default: 1.0)\n");

    std::printf("\nAvailable benchmarks:\n");
    for (const auto& name : list_combat_benchmarks()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable models:\n");
    for (const auto& name : list_combat_solvers()) {
        std::printf("  %s\n", name.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::string solver_name = "v3";
    std::string opponent_name = "v2_knapsack";
    std::string benchmark_name = "corridor";
    float spartan = 1.0f;

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--opponent=", 11) == 0)
            opponent_name = argv[i] + 11;
        else if (std::strncmp(argv[i], "--benchmark=", 12) == 0)
            benchmark_name = argv[i] + 12;
        else if (std::strncmp(argv[i], "--spartan=", 10) == 0)
            spartan = static_cast<float>(std::atof(argv[i] + 10));
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    auto bm = get_combat_benchmark(benchmark_name);
    if (!bm) {
        std::fprintf(stderr, "Unknown benchmark: %s\n", benchmark_name.c_str());
        print_usage();
        return 1;
    }

    const CombatSolver* solver_factory = get_combat_solver(solver_name);
    const CombatSolver* opponent_factory = get_combat_solver(opponent_name);
    if (!solver_factory) { std::fprintf(stderr, "Unknown solver: %s\n", solver_name.c_str()); return 1; }
    if (!opponent_factory) { std::fprintf(stderr, "Unknown opponent: %s\n", opponent_name.c_str()); return 1; }

    auto solver_ai = (*solver_factory)(0);
    auto opponent_ai = (*opponent_factory)(1);

    auto* dist_player = dynamic_cast<DistributionAIPlayer*>(solver_ai.get());
    if (dist_player) dist_player->set_metrics_enabled(true);

    // Construct game
    Game game(bm->config, bm->graph, bm->capitals);
    for (const auto& ov : bm->overrides) {
        game.set_node_state(ov.node_idx, ov.state, ov.owner, ov.troops);
    }
    if (spartan != 1.0f) {
        for (int i = 0; i < game.graph().num_nodes(); i++) {
            const auto& nd = game.node_data()[i];
            if (nd.owner == 1 && 1 < static_cast<int>(nd.troops.size())) {
                int scaled = static_cast<int>(nd.troops[1] * spartan);
                game.set_node_state(i, nd.state, nd.owner, scaled);
            }
        }
    }

    // --- VizApp ---
    VizApp app("CRisky Combat Gym Viz", 1600, 900);
    app.init_camera(game.graph());

    // --- Ring buffers ---
    RingBuffer<float> p0_troops(4096), p1_troops(4096);
    RingBuffer<float> p0_nodes(4096), p1_nodes(4096);
    RingBuffer<float> p0_kd(4096), p1_kd(4096);
    RingBuffer<float> p0_kills(4096), p1_kills(4096);

    // --- Compose panels ---
    PanelHost host;

    // Keep raw pointers for per-frame color sync with theme
    std::vector<TimeSeriesChart*> player_charts;

    auto charts_tab = std::make_unique<TabbedPanel>("Charts");
    {
        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        unsigned int c0 = color_to_im(scheme.player_colors[0]);
        unsigned int c1 = color_to_im(scheme.player_colors[1]);

        auto troops_chart = std::make_unique<TimeSeriesChart>("Troops", "Tick", "Count");
        troops_chart->add_series("P0 Troops", c0, &p0_troops);
        troops_chart->add_series("P1 Troops", c1, &p1_troops);
        player_charts.push_back(troops_chart.get());
        charts_tab->add_tab(std::move(troops_chart));

        auto territory_chart = std::make_unique<TimeSeriesChart>("Territory", "Tick", "Nodes");
        territory_chart->add_series("P0 Nodes", c0, &p0_nodes);
        territory_chart->add_series("P1 Nodes", c1, &p1_nodes);
        player_charts.push_back(territory_chart.get());
        charts_tab->add_tab(std::move(territory_chart));

        auto kd_chart = std::make_unique<TimeSeriesChart>("K/D Ratio", "Tick", "Ratio");
        kd_chart->add_series("P0 K/D", c0, &p0_kd);
        kd_chart->add_series("P1 K/D", c1, &p1_kd);
        player_charts.push_back(kd_chart.get());
        charts_tab->add_tab(std::move(kd_chart));

        auto kills_chart = std::make_unique<TimeSeriesChart>("Cumulative Kills", "Tick", "Kills");
        kills_chart->add_series("P0 Kills", c0, &p0_kills);
        kills_chart->add_series("P1 Kills", c1, &p1_kills);
        player_charts.push_back(kills_chart.get());
        charts_tab->add_tab(std::move(kills_chart));
    }
    host.add(std::move(charts_tab));

    if (dist_player) {
        host.add(std::make_unique<GraphHeatmap>(
            "P0 Distribution", &game.graph(),
            [&]() -> std::vector<float> {
                const auto& snap = dist_player->decision_snapshot();
                return snap.smoothed;
            }
        ));
        host.add(std::make_unique<GraphHeatmap>(
            "P0 Potential", &game.graph(),
            [&]() -> std::vector<float> {
                const auto& snap = dist_player->decision_snapshot();
                return snap.gradient;
            }
        ));
    }

    int cum_kills_p0 = 0, cum_kills_p1 = 0;
    int cum_deaths_p0 = 0, cum_deaths_p1 = 0;
    int tick_count = 0;
    host.add(std::make_unique<StatsTable>("Summary", [&]() {
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [](const char* f, auto v) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), f, v);
            return std::string(buf);
        };
        rows.push_back({"Tick", fmt("%d", tick_count)});
        rows.push_back({"P0 Kills", fmt("%d", cum_kills_p0)});
        rows.push_back({"P0 Deaths", fmt("%d", cum_deaths_p0)});
        rows.push_back({"P1 Kills", fmt("%d", cum_kills_p1)});
        rows.push_back({"P1 Deaths", fmt("%d", cum_deaths_p1)});
        return rows;
    }));

    host.add(std::make_unique<PlaybackControls>(&app.speed(), &app.paused(), &tick_count));

    // --- Game loop ---
    PassivePlayer passive;
    int n_total = game.n_players();
    int n_real = game.n_real_players();
    std::vector<PlayerInterface*> players(n_total, &passive);
    players[0] = solver_ai.get();
    if (n_real > 1) players[1] = opponent_ai.get();

    while (!app.should_close()) {
        // --- Simulation ---
        float tick_dt;
        while ((tick_dt = app.consume_tick()) > 0) {
            auto metrics = combat_gym_tick(game, players, tick_dt);
            tick_count++;

            // Update cumulative stats
            int d0 = (0 < static_cast<int>(metrics.deaths.size())) ? metrics.deaths[0] : 0;
            int d1 = (1 < static_cast<int>(metrics.deaths.size())) ? metrics.deaths[1] : 0;
            cum_deaths_p0 += d0;
            cum_deaths_p1 += d1;
            cum_kills_p0 += d1;
            cum_kills_p1 += d0;

            int troops_0 = (0 < static_cast<int>(metrics.troops.size())) ? metrics.troops[0] : 0;
            int troops_1 = (1 < static_cast<int>(metrics.troops.size())) ? metrics.troops[1] : 0;
            float territory_0 = (0 < static_cast<int>(metrics.territory.size())) ? metrics.territory[0] : 0.0f;
            float territory_1 = (1 < static_cast<int>(metrics.territory.size())) ? metrics.territory[1] : 0.0f;

            p0_troops.push(static_cast<float>(troops_0));
            p1_troops.push(static_cast<float>(troops_1));
            p0_nodes.push(territory_0);
            p1_nodes.push(territory_1);
            p0_kd.push(cum_deaths_p0 > 0 ? static_cast<float>(cum_kills_p0) / cum_deaths_p0 : 0.0f);
            p1_kd.push(cum_deaths_p1 > 0 ? static_cast<float>(cum_kills_p1) / cum_deaths_p1 : 0.0f);
            p0_kills.push(static_cast<float>(cum_kills_p0));
            p1_kills.push(static_cast<float>(cum_kills_p1));
        }

        // --- Rendering ---
        app.begin_frame();
        app.draw_game(game);

        if (game.is_game_over()) {
            DrawText("GAME OVER", app.screen_w() / 2 - 80, 10, 24, RED);
        }
        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        Color status_color = scheme.sys_color();
        status_color.a = 200;
        DrawText(TextFormat("Tick: %d  Speed: %.0fx", tick_count, app.speed()),
                 10, app.screen_h() - 30, 16, status_color);

        // Sync chart colors with current theme
        {
            unsigned int c0 = color_to_im(scheme.player_colors[0]);
            unsigned int c1 = color_to_im(scheme.player_colors[1]);
            for (auto* chart : player_charts) {
                chart->set_series_color(0, c0);
                chart->set_series_color(1, c1);
            }
        }

        app.begin_imgui();
        host.draw();
        app.end_imgui();

        app.end_frame();
    }

    // Print final results
    int nodes_0 = 0, nodes_1 = 0;
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        if (game.node_data()[i].owner == 0) nodes_0++;
        if (game.node_data()[i].owner == 1) nodes_1++;
    }
    std::printf("\n=== Combat Gym Viz Results ===\n");
    std::printf("Ticks: %d\n", tick_count);
    std::printf("Nodes: P0=%d, P1=%d\n", nodes_0, nodes_1);
    std::printf("Kills: P0=%d, P1=%d\n", cum_kills_p0, cum_kills_p1);
    std::printf("Deaths: P0=%d, P1=%d\n", cum_deaths_p0, cum_deaths_p1);

    return 0;
}
