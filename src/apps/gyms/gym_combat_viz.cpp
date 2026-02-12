#include "gyms/combat_benchmarks.hpp"
#include "systems/combat/combat_solvers.hpp"
#include "ai/players/passive_player.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "engine/game.hpp"

#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"

#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/bar_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>

static void print_usage() {
    std::printf("Usage: gym_combat_viz [options]\n");
    std::printf("  --solver=NAME      AI model for player 0 (default: v3_bootstrap)\n");
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
    std::string solver_name = "v3_bootstrap";
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

    // Enable metrics on solver if it's a DistributionAIPlayer
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

    // --- Window setup ---
    int screen_w = 1600, screen_h = 900;
    InitWindow(screen_w, screen_h, "CRisky Combat Gym Viz");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    Renderer renderer(screen_w, screen_h);
    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    // --- Ring buffers (per-tick data) ---
    RingBuffer<float> p0_troops(4096), p1_troops(4096);
    RingBuffer<float> p0_nodes(4096), p1_nodes(4096);
    RingBuffer<float> p0_kd(4096), p1_kd(4096);
    RingBuffer<float> p0_kills(4096), p1_kills(4096);

    // --- Compose panels ---
    PanelHost host;

    // Troops over time
    auto troops_chart = std::make_unique<TimeSeriesChart>("Troops", "Tick", "Count");
    troops_chart->add_series("P0 Troops", IM_COL32(100, 149, 237, 255), &p0_troops);
    troops_chart->add_series("P1 Troops", IM_COL32(255, 99, 71, 255), &p1_troops);
    host.add(std::move(troops_chart));

    // Territory over time
    auto territory_chart = std::make_unique<TimeSeriesChart>("Territory", "Tick", "Nodes");
    territory_chart->add_series("P0 Nodes", IM_COL32(100, 149, 237, 255), &p0_nodes);
    territory_chart->add_series("P1 Nodes", IM_COL32(255, 99, 71, 255), &p1_nodes);
    host.add(std::move(territory_chart));

    // K/D ratio over time
    auto kd_chart = std::make_unique<TimeSeriesChart>("K/D Ratio", "Tick", "Ratio");
    kd_chart->add_series("P0 K/D", IM_COL32(100, 149, 237, 255), &p0_kd);
    kd_chart->add_series("P1 K/D", IM_COL32(255, 99, 71, 255), &p1_kd);
    host.add(std::move(kd_chart));

    // Cumulative kills
    auto kills_chart = std::make_unique<TimeSeriesChart>("Cumulative Kills", "Tick", "Kills");
    kills_chart->add_series("P0 Kills", IM_COL32(100, 149, 237, 255), &p0_kills);
    kills_chart->add_series("P1 Kills", IM_COL32(255, 99, 71, 255), &p1_kills);
    host.add(std::move(kills_chart));

    // Distribution heatmap (only if solver is DistributionAIPlayer)
    if (dist_player) {
        host.add(std::make_unique<GraphHeatmap>(
            "Distribution", &game.graph(),
            [&]() -> std::vector<float> {
                const auto& snap = dist_player->decision_snapshot();
                return snap.smoothed;
            }
        ));

        host.add(std::make_unique<GraphHeatmap>(
            "Gradient", &game.graph(),
            [&]() -> std::vector<float> {
                const auto& snap = dist_player->decision_snapshot();
                return snap.gradient;
            }
        ));
    }

    // Stats table
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

    // Playback controls
    float speed = 1.0f;
    bool paused = false;
    host.add(std::make_unique<PlaybackControls>(&speed, &paused, &tick_count));

    // --- Game loop ---
    int n_total = game.n_players();
    int n_real = game.n_real_players();
    std::vector<PlayerCommands> commands(n_total);
    PassivePlayer passive;
    bool game_over = false;

    float tick_accumulator = 0.0f;

    while (!WindowShouldClose()) {
        // --- Simulation ---
        if (!paused && !game_over) {
            tick_accumulator += speed;
            while (tick_accumulator >= 1.0f) {
                tick_accumulator -= 1.0f;

                for (auto& c : commands) c = PlayerCommands{};

                for (int p = 0; p < n_total; p++) {
                    if (!game.is_alive(p)) continue;
                    if (p == 0)
                        solver_ai->decide(game, p, commands[p]);
                    else if (p < n_real)
                        opponent_ai->decide(game, p, commands[p]);
                    else
                        passive.decide(game, p, commands[p]);
                }

                // Pure combat — no building
                for (auto& c : commands) c.builds.clear();

                game.tick(1.0f, commands);
                tick_count++;

                // Update cumulative stats
                const auto& deaths = game.tick_deaths();
                int d0 = (0 < static_cast<int>(deaths.size())) ? deaths[0] : 0;
                int d1 = (1 < static_cast<int>(deaths.size())) ? deaths[1] : 0;
                cum_deaths_p0 += d0;
                cum_deaths_p1 += d1;
                cum_kills_p0 += d1;  // p0's kills = p1's deaths
                cum_kills_p1 += d0;

                // Count troops and territory
                int troops_0 = 0, troops_1 = 0, nodes_0 = 0, nodes_1 = 0;
                for (int i = 0; i < game.graph().num_nodes(); i++) {
                    const auto& nd = game.node_data()[i];
                    if (nd.owner == 0) nodes_0++;
                    if (nd.owner == 1) nodes_1++;
                    if (0 < static_cast<int>(nd.troops.size())) troops_0 += nd.troops[0];
                    if (1 < static_cast<int>(nd.troops.size())) troops_1 += nd.troops[1];
                }
                // Add in-transit troops
                for (const auto& el : game.edge_lanes()) {
                    for (int lane = 0; lane < 2; lane++) {
                        for (const auto& g : el.lanes[lane].groups) {
                            if (g.owner == 0) troops_0 += g.count;
                            if (g.owner == 1) troops_1 += g.count;
                        }
                    }
                }

                // Push to ring buffers
                p0_troops.push(static_cast<float>(troops_0));
                p1_troops.push(static_cast<float>(troops_1));
                p0_nodes.push(static_cast<float>(nodes_0));
                p1_nodes.push(static_cast<float>(nodes_1));
                p0_kd.push(cum_deaths_p0 > 0 ? static_cast<float>(cum_kills_p0) / cum_deaths_p0 : 0.0f);
                p1_kd.push(cum_deaths_p1 > 0 ? static_cast<float>(cum_kills_p1) / cum_deaths_p1 : 0.0f);
                p0_kills.push(static_cast<float>(cum_kills_p0));
                p1_kills.push(static_cast<float>(cum_kills_p1));

                if (game.is_game_over()) {
                    game_over = true;
                    break;
                }

                if (tick_count >= bm->max_ticks) {
                    game_over = true;
                    break;
                }
            }
        }

        // Re-pause after a single step
        if (paused) tick_accumulator = 0.0f;

        camera.update();

        // --- Rendering ---
        BeginDrawing();
        ClearBackground(BLACK);

        // Game world
        BeginMode2D(Camera2D{
            .offset = camera.offset(),
            .target = {0, 0},
            .rotation = camera.rotation(),
            .zoom = camera.zoom()
        });
        renderer.draw(game, camera);
        EndMode2D();

        // Status bar
        if (game_over) {
            DrawText("GAME OVER", screen_w / 2 - 80, 10, 24, RED);
        }
        DrawText(TextFormat("Tick: %d  Speed: %.0fx", tick_count, speed),
                 10, screen_h - 30, 16, LIGHTGRAY);

        // ImGui frame
        rlImGuiBegin();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        host.draw();
        rlImGuiEnd();

        EndDrawing();
    }

    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();

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
