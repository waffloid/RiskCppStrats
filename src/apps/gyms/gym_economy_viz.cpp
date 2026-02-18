#include "gyms/economy_gym.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "engine/game.hpp"

#include "viz/viz_app.hpp"
#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"

#include "viz/imgui_theme.hpp"

#include "raylib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>

// Helper: update heatmap data from node state
static void update_heatmaps(const Graph& graph, const std::vector<NodeData>& nodes,
                            const GameConfig& config,
                            std::vector<float>& building_type,
                            std::vector<float>& production_contrib) {
    int n = graph.num_nodes();
    building_type.resize(n);
    production_contrib.resize(n);

    for (int i = 0; i < n; i++) {
        switch (nodes[i].state) {
            case NodeState::CAPITAL:    building_type[i] = 0.0f; break;
            case NodeState::FACTORY:    building_type[i] = 1.0f; break;
            case NodeState::POWERPLANT: building_type[i] = 2.0f; break;
            default:                    building_type[i] = -1.0f; break;
        }

        float base = 0.0f;
        if (nodes[i].state == NodeState::CAPITAL)
            base = static_cast<float>(config.capital_troops_per_tick);
        else if (nodes[i].state == NodeState::FACTORY)
            base = static_cast<float>(config.factory_troops_per_tick);

        int pp_count = 0;
        if (base > 0) {
            for (int nbr : graph.neighbors(i)) {
                if (nodes[nbr].state == NodeState::POWERPLANT && nodes[nbr].owner == 0) {
                    pp_count++;
                }
            }
        }
        production_contrib[i] = base + static_cast<float>(pp_count * config.powerplant_bonus);
    }
}

// Helper: update display game from node state
static std::unique_ptr<Game> make_display_game(const GameConfig& config,
                                                const Graph& graph,
                                                const std::vector<NodeData>& nodes) {
    auto game = std::make_unique<Game>(config, Graph(graph), std::vector<int>{0});
    for (int i = 0; i < graph.num_nodes(); i++) {
        game->set_node_state(i, nodes[i].state, 0,
                             nodes[i].troops.empty() ? 0 : nodes[i].troops[0]);
    }
    return game;
}

// Helper: prepare blank nodes for a graph (all owned, enough troops, node 0 = capital)
static std::vector<NodeData> make_blank_nodes(const Graph& graph, const GameConfig& config) {
    int n = graph.num_nodes();
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {config.cost_powerplant + 1};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;
    return nodes;
}

static void print_usage() {
    std::printf("Usage: gym_economy_viz [options]\n");
    std::printf("  --solver=NAME      greedy, bootstrap, mcmc (default: mcmc)\n");
    std::printf("  --seed=N           Random seed (default: 42)\n");
    std::printf("  --iters=N          MCMC iterations (default: 2000)\n");
    std::printf("  --temp=F           MCMC initial temperature (default: 5.0)\n");
    std::printf("  --speed=N          MCMC iterations per frame (default: 10)\n");
    std::printf("  --nodes=N          Approximate node count (default: 50)\n");
    std::printf("  --edge-dist=F      Edge distance threshold (default: 20.0)\n");
    std::printf("  --max-nbrs=N       Max neighbors per node (default: 6)\n");
    std::printf("  --region=F         Region width & height (default: 100.0)\n");
}

int main(int argc, char* argv[]) {
    std::string solver_name = "mcmc";
    uint64_t seed = 42;
    int mcmc_iters = 2000;
    float mcmc_temp = 5.0f;
    int iters_per_frame = 10;
    int n_nodes_hint = 50;
    GameConfig config{};

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        else if (std::strncmp(argv[i], "--iters=", 8) == 0)
            mcmc_iters = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--temp=", 7) == 0)
            mcmc_temp = static_cast<float>(std::atof(argv[i] + 7));
        else if (std::strncmp(argv[i], "--speed=", 8) == 0)
            iters_per_frame = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--nodes=", 8) == 0)
            n_nodes_hint = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--edge-dist=", 12) == 0)
            config.edge_distance_threshold = static_cast<float>(std::atof(argv[i] + 12));
        else if (std::strncmp(argv[i], "--max-nbrs=", 11) == 0)
            config.max_neighbors = std::atoi(argv[i] + 11);
        else if (std::strncmp(argv[i], "--region=", 9) == 0) {
            float r = static_cast<float>(std::atof(argv[i] + 9));
            config.region_width = r;
            config.region_height = r;
        }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    // Generate initial graph
    GameConfig gen_config = config;
    if (n_nodes_hint > 0) {
        float area = gen_config.region_width * gen_config.region_height;
        gen_config.poisson_intensity = static_cast<float>(n_nodes_hint) / area;
    }
    Game init_game(gen_config, {0}, seed);
    Graph graph = init_game.graph();
    auto blank_nodes = make_blank_nodes(graph, config);

    // Run non-MCMC solver for initial state (or blank for MCMC)
    std::vector<NodeData> current_nodes = blank_nodes;
    if (solver_name != "mcmc") {
        EconomySolver solver = get_economy_solver(solver_name);
        if (solver) {
            auto plan = solver(graph, blank_nodes, 0, config);
            for (const auto& step : plan.steps) {
                if (step.node_idx >= 0 && step.node_idx < graph.num_nodes())
                    current_nodes[step.node_idx].state = step.structure;
            }
        }
    }

    // MCMC stepper (created for mcmc mode)
    std::unique_ptr<MCMCStepper> stepper;
    if (solver_name == "mcmc") {
        stepper = std::make_unique<MCMCStepper>(
            graph, blank_nodes, 0, config, mcmc_iters, mcmc_temp);
        current_nodes = stepper->current_state();
    }

    // Heatmap data
    std::vector<float> building_type, production_contrib;
    update_heatmaps(graph, current_nodes, config, building_type, production_contrib);

    // Display game
    auto display_game = make_display_game(config, graph, current_nodes);

    // --- VizApp ---
    VizApp app("CRisky Economy Gym Viz", 1400, 800);
    app.init_camera(graph);

    // --- MCMC trace ring buffers ---
    RingBuffer<float> buf_mcmc_prod(16384);
    RingBuffer<float> buf_mcmc_best(16384);

    // --- Compose panels ---
    PanelHost host;

    const auto& init_scheme = COLOR_SCHEMES[app.scheme_idx()];
    auto mcmc_chart_ptr = std::make_unique<TimeSeriesChart>("MCMC Trace", "Iteration", "Production");
    mcmc_chart_ptr->add_series("Current", scheme_metric_color(init_scheme, 0), &buf_mcmc_prod);
    mcmc_chart_ptr->add_series("Best", scheme_metric_color(init_scheme, 1), &buf_mcmc_best);
    auto* mcmc_chart = mcmc_chart_ptr.get();
    host.add(std::move(mcmc_chart_ptr));

    // Heatmap pointers point to graph — must update when graph changes
    const Graph* graph_ptr = &graph;
    host.add(std::make_unique<GraphHeatmap>(
        "Building Types", graph_ptr,
        [&]() -> std::vector<float> { return building_type; }
    ));
    host.add(std::make_unique<GraphHeatmap>(
        "Production Map", graph_ptr,
        [&]() -> std::vector<float> { return production_contrib; }
    ));

    // Stats
    float current_production = 0.0f;
    float theoretical_max = 0.0f;
    int n_factories = 0, n_powerplants = 0;

    auto update_stats = [&]() {
        current_production = compute_production_rate(graph, current_nodes, 0, config);
        theoretical_max = compute_theoretical_max_production(graph, current_nodes, 0, config);
        n_factories = 0; n_powerplants = 0;
        for (int i = 0; i < graph.num_nodes(); i++) {
            if (current_nodes[i].state == NodeState::FACTORY) n_factories++;
            if (current_nodes[i].state == NodeState::POWERPLANT) n_powerplants++;
        }
    };
    update_stats();

    host.add(std::make_unique<StatsTable>("Economy Stats", [&]() {
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [](const char* f, auto v) {
            char buf[64]; std::snprintf(buf, sizeof(buf), f, v); return std::string(buf);
        };
        rows.push_back({"Solver", solver_name});
        rows.push_back({"Seed", fmt("%lu", static_cast<unsigned long>(seed))});
        rows.push_back({"Nodes", fmt("%d", graph.num_nodes())});
        rows.push_back({"Edges", fmt("%d", graph.num_edges())});
        rows.push_back({"Factories", fmt("%d", n_factories)});
        rows.push_back({"Powerplants", fmt("%d", n_powerplants)});
        rows.push_back({"Production", fmt("%.1f", current_production)});
        rows.push_back({"Max Possible", fmt("%.1f", theoretical_max)});
        float eff = (theoretical_max > 0) ? current_production / theoretical_max : 0.0f;
        rows.push_back({"Efficiency", fmt("%.1f%%", eff * 100.0f)});
        if (stepper) {
            rows.push_back({"Iteration", fmt("%d / %d", stepper->iteration())});
            rows.push_back({"Best Prod", fmt("%.1f", stepper->best_production())});
            rows.push_back({"Accepted", fmt("%d", stepper->trace().accepted)});
            rows.push_back({"Improvements", fmt("%d", stepper->trace().improvements)});
        }
        return rows;
    }));

    int dummy_tick = 0;
    host.add(std::make_unique<PlaybackControls>(&app.speed(), &app.paused(), &dummy_tick));

    // --- Main loop ---
    bool needs_new_graph = false;

    while (!app.should_close()) {
        // --- MCMC stepping (when unpaused) ---
        if (stepper && !app.paused()) {
            int steps = std::max(1, static_cast<int>(iters_per_frame * app.speed()));
            int prev_iter = stepper->iteration();
            stepper->step(steps);

            // Push new trace data to ring buffers
            const auto& trace = stepper->trace();
            for (int i = prev_iter; i < stepper->iteration() && i < static_cast<int>(trace.production_per_iter.size()); i++) {
                buf_mcmc_prod.push(trace.production_per_iter[i]);
                buf_mcmc_best.push(trace.best_production_per_iter[i]);
            }

            // Update display from current MCMC state
            current_nodes = stepper->current_state();
            update_heatmaps(graph, current_nodes, config, building_type, production_contrib);
            update_stats();
            display_game = make_display_game(config, graph, current_nodes);
            dummy_tick = stepper->iteration();
        }

        // --- Rendering ---
        app.begin_frame();
        app.draw_game(*display_game);

        float eff = (theoretical_max > 0) ? current_production / theoretical_max : 0.0f;
        const char* status = app.paused() ? "  [PAUSED]" : "";
        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        Color status_color = scheme.sys_color();
        status_color.a = 200;
        DrawText(TextFormat("Solver: %s  Seed: %lu  Production: %.1f  Efficiency: %.0f%%%s",
                            solver_name.c_str(),
                            static_cast<unsigned long>(seed),
                            current_production, eff * 100.0f, status),
                 10, app.screen_h() - 30, 16, status_color);

        // Sync metric chart colors with theme
        mcmc_chart->set_series_color(0, scheme_metric_color(scheme, 0));
        mcmc_chart->set_series_color(1, scheme_metric_color(scheme, 1));

        app.begin_imgui();
        host.draw();

        // Control panel
        if (ImGui::Begin("Controls")) {
            if (ImGui::Button("Reset MCMC")) {
                if (stepper) {
                    stepper->reset(mcmc_iters, mcmc_temp);
                    buf_mcmc_prod = RingBuffer<float>(16384);
                    buf_mcmc_best = RingBuffer<float>(16384);
                    current_nodes = stepper->current_state();
                    update_heatmaps(graph, current_nodes, config, building_type, production_contrib);
                    update_stats();
                    display_game = make_display_game(config, graph, current_nodes);
                    dummy_tick = 0;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("New Graph")) {
                seed++;
                needs_new_graph = true;
            }

            ImGui::Separator();

            const char* solvers[] = {"greedy", "bootstrap", "mcmc"};
            int current_solver = 0;
            for (int i = 0; i < 3; i++) {
                if (solver_name == solvers[i]) current_solver = i;
            }
            if (ImGui::Combo("Solver", &current_solver, solvers, 3)) {
                solver_name = solvers[current_solver];
                needs_new_graph = true;
            }

            int seed_int = static_cast<int>(seed);
            if (ImGui::InputInt("Seed", &seed_int)) {
                seed = static_cast<uint64_t>(std::max(0, seed_int));
                needs_new_graph = true;
            }

            ImGui::Separator();
            ImGui::Text("Graph Parameters");
            if (ImGui::SliderInt("Nodes", &n_nodes_hint, 5, 500)) {
                needs_new_graph = true;
            }
            if (ImGui::SliderFloat("Edge Dist", &config.edge_distance_threshold, 5.0f, 50.0f)) {
                needs_new_graph = true;
            }
            if (ImGui::SliderInt("Max Neighbors", &config.max_neighbors, 2, 12)) {
                needs_new_graph = true;
            }
            float region = config.region_width;
            if (ImGui::SliderFloat("Region Size", &region, 50.0f, 500.0f)) {
                config.region_width = region;
                config.region_height = region;
                needs_new_graph = true;
            }

            if (solver_name == "mcmc") {
                ImGui::Separator();
                ImGui::Text("MCMC Parameters");
                if (stepper) {
                    ImGui::SliderFloat("Temperature", &stepper->initial_temp(), 0.1f, 20.0f);
                    ImGui::SliderInt("Cooling Rate", &stepper->cooling_rate(), 100, 50000);
                } else {
                    ImGui::SliderFloat("Temperature", &mcmc_temp, 0.1f, 20.0f);
                    ImGui::SliderInt("Cooling Rate", &mcmc_iters, 100, 50000);
                }
                ImGui::SliderInt("Iters/Frame", &iters_per_frame, 1, 200);
            }
        }
        ImGui::End();

        app.end_imgui();
        app.end_frame();

        // Rebuild graph if needed
        if (needs_new_graph) {
            gen_config = config;
            if (n_nodes_hint > 0) {
                float area = gen_config.region_width * gen_config.region_height;
                gen_config.poisson_intensity = static_cast<float>(n_nodes_hint) / area;
            }
            Game new_game(gen_config, {0}, seed);
            graph = new_game.graph();
            blank_nodes = make_blank_nodes(graph, config);
            current_nodes = blank_nodes;

            if (solver_name == "mcmc") {
                stepper = std::make_unique<MCMCStepper>(
                    graph, blank_nodes, 0, config, mcmc_iters, mcmc_temp);
                current_nodes = stepper->current_state();
            } else {
                stepper.reset();
                EconomySolver solver = get_economy_solver(solver_name);
                if (solver) {
                    auto plan = solver(graph, blank_nodes, 0, config);
                    for (const auto& step : plan.steps)
                        if (step.node_idx >= 0 && step.node_idx < graph.num_nodes())
                            current_nodes[step.node_idx].state = step.structure;
                }
            }

            buf_mcmc_prod = RingBuffer<float>(16384);
            buf_mcmc_best = RingBuffer<float>(16384);
            update_heatmaps(graph, current_nodes, config, building_type, production_contrib);
            update_stats();
            display_game = make_display_game(config, graph, current_nodes);
            app.init_camera(graph);
            dummy_tick = 0;
            needs_new_graph = false;
        }
    }

    std::printf("\n=== Economy Gym Viz Results ===\n");
    std::printf("Solver: %s, Seed: %lu\n", solver_name.c_str(),
                static_cast<unsigned long>(seed));
    std::printf("Production: %.1f\n", current_production);

    return 0;
}
