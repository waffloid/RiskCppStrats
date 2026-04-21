#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/ot_solver.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/game.hpp"

#include "viz/viz_app.hpp"
#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"
#include "viz/panels/tabbed_panel.hpp"
#include "viz/imgui_theme.hpp"

#include "raylib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <memory>
#include <vector>

static void print_usage() {
    std::printf("Usage: gym_transport_viz [options]\n");
    std::printf("  --preset=NAME    Transport preset (default: star_center)\n");
    std::printf("  --solver=NAME    Transport solver (default: greedy)\n");
    std::printf("  --loss=NAME      Loss function (default: l1)\n");
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

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--preset=", 9) == 0)
            preset_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--loss=", 7) == 0)
            loss_name = argv[i] + 7;
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
    TransportPreset preset = *preset_opt;  // mutable — mouse-follow overwrites target

    // OT solver needs graph+target at construction; rebuilt when target changes (mouse-follow).
    // Shortest paths are precomputed once and reused across rebuilds.
    ShortestPathData ot_sp;
    std::shared_ptr<OTSolver> ot_solver_ptr;
    TransportSolver solver;
    if (solver_name == "ot") {
        ot_sp = compute_shortest_paths(preset.graph);
        ot_solver_ptr = std::make_shared<OTSolver>(preset.graph, preset.target_troops, ot_sp);
        solver = [&ot_solver_ptr](const Graph&, const std::vector<NodeData>& nodes,
                                   const std::vector<float>&, int pid,
                                   const std::vector<bool>& masked) {
            return ot_solver_ptr->solve(nodes, pid, masked);
        };
    } else {
        solver = get_transport_solver(solver_name);
    }
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

    // --- VizApp ---
    VizApp app("CRisky Transport Gym Viz", 1400, 800);
    app.init_camera(game.graph());

    // --- Ring buffers ---
    RingBuffer<float> buf_loss(4096);
    RingBuffer<float> buf_loss_delta(4096);
    RingBuffer<float> buf_in_transit(4096);

    // --- Per-tick state ---
    std::vector<float> deficit(n, 0.0f);
    std::vector<float> current_frac(n, 0.0f);

    // Static target distribution (normalized)
    std::vector<float> target_frac(n, 0.0f);
    {
        int total_target = 0;
        for (int i = 0; i < n; i++) total_target += preset.target_troops[i];
        if (total_target > 0) {
            for (int i = 0; i < n; i++)
                target_frac[i] = static_cast<float>(preset.target_troops[i]) /
                                 static_cast<float>(total_target);
        }
    }

    // --- Compose panels ---
    PanelHost host;

    std::vector<std::pair<TimeSeriesChart*, int>> metric_charts;  // chart, metric index
    auto charts_tab = std::make_unique<TabbedPanel>("Charts");
    {
        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        auto loss_chart = std::make_unique<TimeSeriesChart>("Loss", "Tick", loss_name);
        loss_chart->add_series("Loss", scheme_metric_color(scheme, 0), &buf_loss);
        metric_charts.push_back({loss_chart.get(), 0});
        charts_tab->add_tab(std::move(loss_chart));

        auto delta_chart = std::make_unique<TimeSeriesChart>("Loss Delta", "Tick", "Delta");
        delta_chart->add_series("Delta", scheme_metric_color(scheme, 1), &buf_loss_delta);
        metric_charts.push_back({delta_chart.get(), 1});
        charts_tab->add_tab(std::move(delta_chart));

        auto transit_chart = std::make_unique<TimeSeriesChart>("In Transit", "Tick", "Troops");
        transit_chart->add_series("Transit", scheme_metric_color(scheme, 2), &buf_in_transit);
        metric_charts.push_back({transit_chart.get(), 2});
        charts_tab->add_tab(std::move(transit_chart));
    }
    host.add(std::move(charts_tab));

    host.add(std::make_unique<GraphHeatmap>(
        "Target Field", &game.graph(),
        [&]() -> std::vector<float> { return target_frac; }
    ));
    host.add(std::make_unique<GraphHeatmap>(
        "Deficit (Flow Gradient)", &game.graph(),
        [&]() -> std::vector<float> { return deficit; }
    ));
    host.add(std::make_unique<GraphHeatmap>(
        "Current Distribution", &game.graph(),
        [&]() -> std::vector<float> { return current_frac; }
    ));

    // Voronoi partition: each node colored by its nearest supply node.
    // Only active when using OT solver.
    std::vector<int> voronoi_cells(n, -1);
    if (ot_solver_ptr) {
        auto voronoi_panel = std::make_unique<GraphHeatmap>(
            "Supply Voronoi", &game.graph(),
            [&]() -> std::vector<float> {
                // Dummy values — actual coloring via node_color_fn
                return std::vector<float>(n, 0.0f);
            }
        );
        voronoi_panel->set_node_color_fn([&]() -> std::vector<unsigned int> {
            // HSV-based distinct colors per demand node
            std::vector<unsigned int> colors(n);
            if (static_cast<int>(voronoi_cells.size()) != n) {
                std::fill(colors.begin(), colors.end(), 0xFF404040u);
                return colors;
            }
            // Collect unique demand node IDs
            std::vector<int> unique_suppliers;
            for (int c : voronoi_cells) {
                if (c < 0) continue;
                bool found = false;
                for (int u : unique_suppliers) {
                    if (u == c) { found = true; break; }
                }
                if (!found) unique_suppliers.push_back(c);
            }
            int num_colors = std::max(1, static_cast<int>(unique_suppliers.size()));

            for (int i = 0; i < n; i++) {
                if (voronoi_cells[i] < 0) {
                    colors[i] = 0xFF404040;  // gray for unassigned
                    continue;
                }
                // Find index of this supplier
                int idx = 0;
                for (int k = 0; k < static_cast<int>(unique_suppliers.size()); k++) {
                    if (unique_suppliers[k] == voronoi_cells[i]) { idx = k; break; }
                }
                // HSV → RGB with fixed S=0.7, V=0.9
                float hue = static_cast<float>(idx) / static_cast<float>(num_colors);
                float h = hue * 6.0f;
                float s = 0.7f, v = 0.9f;
                float c = v * s;
                float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
                float m = v - c;
                float r1, g1, b1;
                if (h < 1) { r1 = c; g1 = x; b1 = 0; }
                else if (h < 2) { r1 = x; g1 = c; b1 = 0; }
                else if (h < 3) { r1 = 0; g1 = c; b1 = x; }
                else if (h < 4) { r1 = 0; g1 = x; b1 = c; }
                else if (h < 5) { r1 = x; g1 = 0; b1 = c; }
                else { r1 = c; g1 = 0; b1 = x; }
                auto to_byte = [](float f) -> unsigned int {
                    return static_cast<unsigned int>(f * 255.0f);
                };
                unsigned int r = to_byte(r1 + m);
                unsigned int g = to_byte(g1 + m);
                unsigned int b = to_byte(b1 + m);
                colors[i] = 0xFF000000 | (b << 16) | (g << 8) | r;  // ABGR
            }
            return colors;
        });
        host.add(std::move(voronoi_panel));
    }

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

    host.add(std::make_unique<PlaybackControls>(&app.speed(), &app.paused(), &tick_count));

    // --- Mouse-follow mode ---
    bool mouse_follow = false;
    int total_troops = 0;
    for (int i = 0; i < n; i++) total_troops += preset.initial_troops[i];
    int mouse_node = -1;

    // --- Game loop ---
    std::mt19937 rng(42);
    std::vector<float> warm_phi;
    float prev_loss = 0.0f;

    while (!app.should_close()) {
        // Toggle mouse-follow with M
        if (IsKeyPressed(KEY_M)) {
            mouse_follow = !mouse_follow;
            if (mouse_follow) app.paused() = false;
        }

        // Update target from mouse position
        if (mouse_follow) {
            Vector2 screen_pos = GetMousePosition();
            Vector2 world_pos = app.camera().screen_to_world(screen_pos);

            float best_dist = 1e18f;
            int best_node = 0;
            for (int i = 0; i < n; i++) {
                float dx = game.graph().nodes[i].x - world_pos.x;
                float dy = game.graph().nodes[i].y - world_pos.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_dist) {
                    best_dist = d2;
                    best_node = i;
                }
            }

            if (best_node != mouse_node) {
                mouse_node = best_node;
                preset.target_troops.assign(n, 0);
                preset.target_troops[mouse_node] = total_troops;
                // Update target_frac for heatmap
                target_frac.assign(n, 0.0f);
                target_frac[mouse_node] = 1.0f;
                // Rebuild OT solver with new target (reuses cached shortest paths)
                if (ot_solver_ptr) {
                    ot_solver_ptr = std::make_shared<OTSolver>(preset.graph, preset.target_troops, ot_sp);
                }
            }
        }

        // --- Simulation ---
        float tick_dt;
        while ((tick_dt = app.consume_tick()) > 0) {
            auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi, tick_dt);

            if (tick_count == 0) prev_loss = tr.loss;
            current_loss = tr.loss;
            current_transit = tr.in_transit;

            // Update viz state from tick result
            int total_troops = 0;
            for (int i = 0; i < n; i++) total_troops += tr.current[i];
            for (int i = 0; i < n; i++) {
                deficit[i] = tr.deficit[i];
                current_frac[i] = (total_troops > 0)
                    ? static_cast<float>(tr.current[i]) / static_cast<float>(total_troops)
                    : 0.0f;
            }

            // Update Voronoi from last solve's flow assignment
            if (ot_solver_ptr) {
                voronoi_cells = ot_solver_ptr->last_voronoi();
            }

            buf_loss.push(tr.loss);
            buf_loss_delta.push(tr.loss - prev_loss);
            buf_in_transit.push(static_cast<float>(tr.in_transit));
            prev_loss = tr.loss;
            tick_count++;
        }

        // --- Rendering ---
        app.begin_frame();
        app.draw_game(game);

        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        Color status_color = scheme.sys_color();
        status_color.a = 200;
        const char* mode = mouse_follow ? "MOUSE" : "PRESET";
        DrawText(TextFormat("Tick: %d  Speed: %.0fx  Loss: %.1f  [%s] (M=toggle)",
                            tick_count, app.speed(), current_loss, mode),
                 10, app.screen_h() - 30, 16, status_color);

        // Sync metric chart colors with theme
        for (auto& [chart, idx] : metric_charts)
            chart->set_series_color(0, scheme_metric_color(scheme, idx));

        app.begin_imgui();
        host.draw();
        app.end_imgui();

        app.end_frame();
    }

    // Print final results
    std::printf("\n=== Transport Gym Viz Results ===\n");
    std::printf("Preset: %s, Solver: %s, Loss: %s\n",
                preset_name.c_str(), solver_name.c_str(), loss_name.c_str());
    std::printf("Ticks: %d, Final loss: %.1f\n", tick_count, current_loss);

    return 0;
}
