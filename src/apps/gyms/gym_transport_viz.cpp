#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/game.hpp"

#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"

#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
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
#include <vector>
#include <cmath>

static void print_usage() {
    std::printf("Usage: gym_transport_viz [options]\n");
    std::printf("  --preset=NAME    Transport preset (default: star_center)\n");
    std::printf("  --solver=NAME    Transport solver (default: greedy)\n");
    std::printf("  --loss=NAME      Loss function (default: l1)\n");
    std::printf("  --ticks=N        Max ticks (default: 500)\n");

    std::printf("\nAvailable presets:\n");
    for (const auto& name : list_transport_presets()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable solvers:\n");
    for (const auto& name : list_transport_solvers()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable loss functions:\n");
    for (const auto& name : list_loss_functions()) {
        std::printf("  %s\n", name.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::string preset_name = "star_center";
    std::string solver_name = "greedy";
    std::string loss_name = "l1";
    int max_ticks = 500;

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--preset=", 9) == 0)
            preset_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--loss=", 7) == 0)
            loss_name = argv[i] + 7;
        else if (std::strncmp(argv[i], "--ticks=", 8) == 0)
            max_ticks = std::atoi(argv[i] + 8);
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    auto preset_opt = get_transport_preset(preset_name);
    if (!preset_opt) {
        std::fprintf(stderr, "Unknown preset: %s\n", preset_name.c_str());
        print_usage();
        return 1;
    }
    const TransportPreset& preset = *preset_opt;

    TransportSolver solver = get_transport_solver(solver_name);
    if (!solver) { std::fprintf(stderr, "Unknown solver: %s\n", solver_name.c_str()); return 1; }
    LossFunction loss_fn = get_loss_function(loss_name);
    if (!loss_fn) { std::fprintf(stderr, "Unknown loss: %s\n", loss_name.c_str()); return 1; }

    // Set up single-player game with zero production
    GameConfig config;
    config.init_troop_count = 0;
    config.capital_troops_per_tick = 0;
    config.factory_troops_per_tick = 0;
    config.powerplant_bonus = 0;

    Game game(config, preset.graph, {0});
    int n = preset.graph.num_nodes();
    for (int i = 0; i < n; i++) {
        NodeState state = (i == 0) ? NodeState::CAPITAL : NodeState::DEFAULT;
        game.set_node_state(i, state, 0, preset.initial_troops[i]);
    }

    // --- Window setup ---
    int screen_w = 1400, screen_h = 800;
    InitWindow(screen_w, screen_h, "CRisky Transport Gym Viz");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    Renderer renderer(screen_w, screen_h);
    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    // --- Ring buffers ---
    RingBuffer<float> buf_loss(4096);
    RingBuffer<float> buf_loss_delta(4096);
    RingBuffer<float> buf_in_transit(4096);

    // --- Per-tick state ---
    std::vector<float> deficit(n, 0.0f);      // target - current per node
    std::vector<float> current_frac(n, 0.0f); // current / total per node

    // --- Compose panels ---
    PanelHost host;

    // Loss over time
    auto loss_chart = std::make_unique<TimeSeriesChart>("Loss", "Tick", loss_name);
    loss_chart->add_series("Loss", IM_COL32(255, 99, 71, 255), &buf_loss);
    host.add(std::move(loss_chart));

    // Loss delta over time
    auto delta_chart = std::make_unique<TimeSeriesChart>("Loss Delta", "Tick", "Delta");
    delta_chart->add_series("Delta", IM_COL32(255, 215, 0, 255), &buf_loss_delta);
    host.add(std::move(delta_chart));

    // Troops in transit
    auto transit_chart = std::make_unique<TimeSeriesChart>("In Transit", "Tick", "Troops");
    transit_chart->add_series("Transit", IM_COL32(100, 149, 237, 255), &buf_in_transit);
    host.add(std::move(transit_chart));

    // Deficit heatmap
    host.add(std::make_unique<GraphHeatmap>(
        "Deficit", &game.graph(),
        [&]() -> std::vector<float> { return deficit; }
    ));

    // Current distribution heatmap
    host.add(std::make_unique<GraphHeatmap>(
        "Current Distribution", &game.graph(),
        [&]() -> std::vector<float> { return current_frac; }
    ));

    // Stats table
    int tick_count = 0;
    float current_loss = 0.0f;
    int current_transit = 0;
    host.add(std::make_unique<StatsTable>("Summary", [&]() {
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [](const char* f, auto v) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), f, v);
            return std::string(buf);
        };
        rows.push_back({"Tick", fmt("%d", tick_count)});
        rows.push_back({"Loss", fmt("%.1f", current_loss)});
        rows.push_back({"In Transit", fmt("%d", current_transit)});
        rows.push_back({"Preset", preset_name});
        rows.push_back({"Solver", solver_name});
        rows.push_back({"Nodes", fmt("%d", n)});
        return rows;
    }));

    // Playback controls
    float speed = 1.0f;
    bool paused = false;
    host.add(std::make_unique<PlaybackControls>(&speed, &paused, &tick_count));

    // --- Game loop ---
    std::vector<bool> no_mask(n, false);
    float prev_loss = 0.0f;
    bool done = false;
    float tick_accumulator = 0.0f;

    while (!WindowShouldClose()) {
        // --- Simulation ---
        if (!paused && !done) {
            tick_accumulator += speed;
            while (tick_accumulator >= 1.0f) {
                tick_accumulator -= 1.0f;

                // Current on-node distribution
                std::vector<int> current(n);
                int total_on_nodes = 0;
                for (int i = 0; i < n; i++) {
                    current[i] = game.node_data()[i].troops[0];
                    total_on_nodes += current[i];
                }

                // Compute loss
                float loss = loss_fn(current, preset.target_troops);
                if (tick_count == 0) prev_loss = loss;
                current_loss = loss;

                // Compute deficit & current_frac for heatmaps
                for (int i = 0; i < n; i++) {
                    deficit[i] = static_cast<float>(preset.target_troops[i] - current[i]);
                    current_frac[i] = (total_on_nodes > 0)
                        ? static_cast<float>(current[i]) / static_cast<float>(total_on_nodes)
                        : 0.0f;
                }

                // Count troops in transit
                current_transit = 0;
                for (const auto& el : game.edge_lanes()) {
                    for (int lane = 0; lane < 2; lane++) {
                        for (const auto& g : el.lanes[lane].groups) {
                            if (g.owner == 0) current_transit += g.count;
                        }
                    }
                }

                // Push to ring buffers
                buf_loss.push(loss);
                buf_loss_delta.push(loss - prev_loss);
                buf_in_transit.push(static_cast<float>(current_transit));
                prev_loss = loss;

                // Compute gradient from deficit
                std::vector<float> gradient(n);
                for (int i = 0; i < n; i++) {
                    gradient[i] = static_cast<float>(preset.target_troops[i] - current[i]);
                }

                // Get commands from solver
                auto commands = solver(game.graph(), game.node_data(), gradient, 0, no_mask);

                // Apply via game tick
                std::vector<PlayerCommands> all_commands(game.n_players());
                all_commands[0].troops = std::move(commands);
                game.tick(1.0f, all_commands);
                tick_count++;

                if (tick_count >= max_ticks) {
                    done = true;
                    break;
                }
            }
        }

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
        if (done) {
            DrawText("DONE", screen_w / 2 - 30, 10, 24, GREEN);
        }
        DrawText(TextFormat("Tick: %d/%d  Speed: %.0fx  Loss: %.1f",
                            tick_count, max_ticks, speed, current_loss),
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
    std::printf("\n=== Transport Gym Viz Results ===\n");
    std::printf("Preset: %s, Solver: %s, Loss: %s\n",
                preset_name.c_str(), solver_name.c_str(), loss_name.c_str());
    std::printf("Ticks: %d, Final loss: %.1f\n", tick_count, current_loss);

    return 0;
}
