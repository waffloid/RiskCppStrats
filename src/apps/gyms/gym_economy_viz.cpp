#include "gyms/economy_gym.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "engine/game.hpp"

#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"

#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"

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
#include <algorithm>

// Current economy state (rebuilt on Sample / New Graph)
struct EcoVizState {
    EconomyGymState eco;
    MCMCTrace trace;
    std::string solver_name;
    uint64_t seed = 42;
    int mcmc_iters = 2000;
    float mcmc_temp = 5.0f;

    // Derived per-node data for heatmaps
    std::vector<float> building_type;    // -1=default, 0=capital, 1=factory, 2=powerplant
    std::vector<float> production_contrib; // per-node production contribution

    void run(const GameConfig& config) {
        eco = run_economy_gym(solver_name, seed, 50, config);
        trace = MCMCTrace{};

        // If MCMC, re-run with trace
        if (solver_name == "mcmc") {
            // Re-setup: player 0 owns all nodes
            Graph graph = eco.graph;
            int n = graph.num_nodes();
            std::vector<NodeData> nodes(n);
            for (int i = 0; i < n; i++) {
                nodes[i].owner = 0;
                nodes[i].troops = {config.cost_powerplant + 1};
                nodes[i].state = NodeState::DEFAULT;
            }
            nodes[0].state = NodeState::CAPITAL;

            auto plan = economy_solver_mcmc_traced(
                graph, nodes, 0, config, mcmc_iters, mcmc_temp, &trace);

            // Apply plan to update eco state
            eco.plan = plan;
            for (const auto& step : plan.steps) {
                if (step.node_idx >= 0 && step.node_idx < n) {
                    eco.nodes[step.node_idx].state = step.structure;
                }
            }
            eco.production_rate = compute_production_rate(graph, eco.nodes, 0, config);
            eco.theoretical_max = compute_theoretical_max_production(graph, eco.nodes, 0, config);
            eco.efficiency = (eco.theoretical_max > 0)
                ? eco.production_rate / eco.theoretical_max : 0.0f;

            // Recount buildings
            eco.n_factories = 0;
            eco.n_powerplants = 0;
            for (int i = 0; i < n; i++) {
                if (eco.nodes[i].state == NodeState::FACTORY) eco.n_factories++;
                if (eco.nodes[i].state == NodeState::POWERPLANT) eco.n_powerplants++;
            }
        }

        // Compute per-node data for heatmaps
        int n = eco.graph.num_nodes();
        building_type.resize(n);
        production_contrib.resize(n);

        for (int i = 0; i < n; i++) {
            switch (eco.nodes[i].state) {
                case NodeState::CAPITAL:    building_type[i] = 0.0f; break;
                case NodeState::FACTORY:    building_type[i] = 1.0f; break;
                case NodeState::POWERPLANT: building_type[i] = 2.0f; break;
                default:                    building_type[i] = -1.0f; break;
            }

            // Per-node production contribution
            float base = 0.0f;
            if (eco.nodes[i].state == NodeState::CAPITAL)
                base = static_cast<float>(config.capital_troops_per_tick);
            else if (eco.nodes[i].state == NodeState::FACTORY)
                base = static_cast<float>(config.factory_troops_per_tick);

            bool powered = false;
            if (base > 0) {
                for (int nbr : eco.graph.neighbors(i)) {
                    if (eco.nodes[nbr].state == NodeState::POWERPLANT
                        && eco.nodes[nbr].owner == 0) {
                        powered = true;
                        break;
                    }
                }
            }
            production_contrib[i] = base + (powered ? static_cast<float>(config.powerplant_bonus) : 0.0f);
        }
    }
};

static void print_usage() {
    std::printf("Usage: gym_economy_viz [options]\n");
    std::printf("  --solver=NAME    greedy, bootstrap, mcmc (default: mcmc)\n");
    std::printf("  --seed=N         Random seed (default: 42)\n");
    std::printf("  --iters=N        MCMC iterations (default: 2000)\n");
    std::printf("  --temp=F         MCMC initial temperature (default: 5.0)\n");
}

int main(int argc, char* argv[]) {
    std::string solver_name = "mcmc";
    uint64_t seed = 42;
    int mcmc_iters = 2000;
    float mcmc_temp = 5.0f;

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        else if (std::strncmp(argv[i], "--iters=", 8) == 0)
            mcmc_iters = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--temp=", 7) == 0)
            mcmc_temp = static_cast<float>(std::atof(argv[i] + 7));
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    GameConfig config{};
    EcoVizState state;
    state.solver_name = solver_name;
    state.seed = seed;
    state.mcmc_iters = mcmc_iters;
    state.mcmc_temp = mcmc_temp;
    state.run(config);

    // --- Window setup ---
    int screen_w = 1400, screen_h = 800;
    InitWindow(screen_w, screen_h, "CRisky Economy Gym Viz");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // We need a Game to render via Renderer (it expects Game&).
    // Create a display-only Game from the economy state.
    auto make_display_game = [&]() {
        Game game(config, state.eco.graph, {0});
        for (int i = 0; i < state.eco.graph.num_nodes(); i++) {
            game.set_node_state(i, state.eco.nodes[i].state, 0,
                                state.eco.nodes[i].troops.empty() ? 0 : state.eco.nodes[i].troops[0]);
        }
        return game;
    };

    auto display_game = std::make_unique<Game>(make_display_game());

    Renderer renderer(screen_w, screen_h);
    Camera2D_Custom camera;
    camera.fit_to_graph(display_game->graph(), screen_w, screen_h);

    // --- MCMC trace ring buffer ---
    RingBuffer<float> buf_mcmc_prod(8192);
    RingBuffer<float> buf_mcmc_best(8192);

    auto fill_mcmc_buffers = [&]() {
        buf_mcmc_prod = RingBuffer<float>(8192);
        buf_mcmc_best = RingBuffer<float>(8192);
        for (float v : state.trace.production_per_iter)
            buf_mcmc_prod.push(v);
        for (float v : state.trace.best_production_per_iter)
            buf_mcmc_best.push(v);
    };
    fill_mcmc_buffers();

    // --- Compose panels ---
    PanelHost host;

    // MCMC production trace (only meaningful for mcmc solver)
    auto mcmc_chart = std::make_unique<TimeSeriesChart>("MCMC Trace", "Iteration", "Production");
    mcmc_chart->add_series("Current", IM_COL32(100, 149, 237, 180), &buf_mcmc_prod);
    mcmc_chart->add_series("Best", IM_COL32(50, 205, 50, 255), &buf_mcmc_best);
    host.add(std::move(mcmc_chart));

    // Building type heatmap
    host.add(std::make_unique<GraphHeatmap>(
        "Building Types", &state.eco.graph,
        [&]() -> std::vector<float> { return state.building_type; }
    ));

    // Production contribution heatmap
    host.add(std::make_unique<GraphHeatmap>(
        "Production Map", &state.eco.graph,
        [&]() -> std::vector<float> { return state.production_contrib; }
    ));

    // Stats table
    host.add(std::make_unique<StatsTable>("Economy Stats", [&]() {
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [](const char* f, auto v) {
            char buf[64]; std::snprintf(buf, sizeof(buf), f, v); return std::string(buf);
        };
        rows.push_back({"Solver", state.solver_name});
        rows.push_back({"Seed", fmt("%lu", static_cast<unsigned long>(state.seed))});
        rows.push_back({"Nodes", fmt("%d", state.eco.graph.num_nodes())});
        rows.push_back({"Edges", fmt("%d", state.eco.graph.num_edges())});
        rows.push_back({"Factories", fmt("%d", state.eco.n_factories)});
        rows.push_back({"Powerplants", fmt("%d", state.eco.n_powerplants)});
        rows.push_back({"Production", fmt("%.1f", state.eco.production_rate)});
        rows.push_back({"Max Possible", fmt("%.1f", state.eco.theoretical_max)});
        rows.push_back({"Efficiency", fmt("%.1f%%", state.eco.efficiency * 100.0f)});
        if (state.solver_name == "mcmc") {
            rows.push_back({"MCMC Iters", fmt("%d", state.mcmc_iters)});
            rows.push_back({"MCMC Temp", fmt("%.1f", state.mcmc_temp)});
            rows.push_back({"Accepted", fmt("%d", state.trace.accepted)});
            rows.push_back({"Improvements", fmt("%d", state.trace.improvements)});
        }
        return rows;
    }));

    // --- Main loop ---
    bool needs_rebuild = false;

    while (!WindowShouldClose()) {
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
        renderer.draw(*display_game, camera);
        EndMode2D();

        // Status bar
        DrawText(TextFormat("Solver: %s  Seed: %lu  Production: %.1f  Efficiency: %.0f%%",
                            state.solver_name.c_str(),
                            static_cast<unsigned long>(state.seed),
                            state.eco.production_rate,
                            state.eco.efficiency * 100.0f),
                 10, screen_h - 30, 16, LIGHTGRAY);

        // ImGui frame
        rlImGuiBegin();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        host.draw();

        // Control panel with buttons and sliders
        if (ImGui::Begin("Controls")) {
            if (ImGui::Button("Sample (Re-run solver)")) {
                needs_rebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("New Graph")) {
                state.seed++;
                needs_rebuild = true;
            }

            ImGui::Separator();

            // Solver selection
            const char* solvers[] = {"greedy", "bootstrap", "mcmc"};
            int current_solver = 0;
            for (int i = 0; i < 3; i++) {
                if (state.solver_name == solvers[i]) current_solver = i;
            }
            if (ImGui::Combo("Solver", &current_solver, solvers, 3)) {
                state.solver_name = solvers[current_solver];
                needs_rebuild = true;
            }

            // Seed input
            int seed_int = static_cast<int>(state.seed);
            if (ImGui::InputInt("Seed", &seed_int)) {
                state.seed = static_cast<uint64_t>(std::max(0, seed_int));
                needs_rebuild = true;
            }

            // MCMC parameters (only show for mcmc solver)
            if (state.solver_name == "mcmc") {
                ImGui::Separator();
                ImGui::Text("MCMC Parameters");
                if (ImGui::SliderInt("Iterations", &state.mcmc_iters, 100, 10000)) {
                    needs_rebuild = true;
                }
                if (ImGui::SliderFloat("Temperature", &state.mcmc_temp, 0.1f, 20.0f)) {
                    needs_rebuild = true;
                }
            }
        }
        ImGui::End();

        rlImGuiEnd();
        EndDrawing();

        // Rebuild if needed (after rendering to avoid stale pointers during draw)
        if (needs_rebuild) {
            state.run(config);
            fill_mcmc_buffers();
            display_game = std::make_unique<Game>(make_display_game());
            camera.fit_to_graph(display_game->graph(), screen_w, screen_h);
            needs_rebuild = false;
        }
    }

    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();

    std::printf("\n=== Economy Gym Viz Results ===\n");
    std::printf("Solver: %s, Seed: %lu\n", state.solver_name.c_str(),
                static_cast<unsigned long>(state.seed));
    std::printf("Production: %.1f, Efficiency: %.1f%%\n",
                state.eco.production_rate, state.eco.efficiency * 100.0f);

    return 0;
}
