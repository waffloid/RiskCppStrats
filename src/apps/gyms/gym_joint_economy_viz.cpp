#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/game.hpp"
#include "engine/game_config.hpp"
#include "systems/graph_algo/maxcut_solvers.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"
#include "systems/economy/economy_solvers.hpp"

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

// ── Helpers ──────────────────────────────────────────────────

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

// ── Main ─────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Parse args
    uint64_t seed = 42;
    int n_nodes_hint = 80;
    int sa_iters = 20000;
    float sa_temp = 5.0f;
    int iters_per_frame = 50;
    float mu_cheap = 1.0f;
    float mu_expensive = 1.0f;
    float mu_bridge = 0.1f;
    float cost_bias = 1.0f;
    GameConfig config{};

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        else if (std::strncmp(argv[i], "--nodes=", 8) == 0)
            n_nodes_hint = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--iters=", 8) == 0)
            sa_iters = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--temp=", 7) == 0)
            sa_temp = static_cast<float>(std::atof(argv[i] + 7));
        else if (std::strncmp(argv[i], "--speed=", 8) == 0)
            iters_per_frame = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--mu-cheap=", 11) == 0)
            mu_cheap = static_cast<float>(std::atof(argv[i] + 11));
        else if (std::strncmp(argv[i], "--mu-expensive=", 15) == 0)
            mu_expensive = static_cast<float>(std::atof(argv[i] + 15));
        else if (std::strncmp(argv[i], "--mu-bridge=", 12) == 0)
            mu_bridge = static_cast<float>(std::atof(argv[i] + 12));
        else if (std::strncmp(argv[i], "--cost-bias=", 12) == 0)
            cost_bias = static_cast<float>(std::atof(argv[i] + 12));
        else if (std::strncmp(argv[i], "--edge-dist=", 12) == 0)
            config.edge_distance_threshold = static_cast<float>(std::atof(argv[i] + 12));
        else if (std::strncmp(argv[i], "--max-nbrs=", 11) == 0)
            config.max_neighbors = std::atoi(argv[i] + 11);
        else if (std::strncmp(argv[i], "--region=", 9) == 0) {
            float r = static_cast<float>(std::atof(argv[i] + 9));
            config.region_width = r;
            config.region_height = r;
        }
    }

    // Generate graph
    GameConfig gen_config = config;
    if (n_nodes_hint > 0) {
        float area = gen_config.region_width * gen_config.region_height;
        gen_config.poisson_intensity = static_cast<float>(n_nodes_hint) / area;
    }
    Game init_game(gen_config, {0}, seed);
    Graph graph = init_game.graph();
    auto blank_nodes = make_blank_nodes(graph, config);

    // Build joint QUBO and stepper
    auto joint = qubo_objective_joint(graph, blank_nodes, 0, config,
                                      mu_cheap, mu_expensive, mu_bridge, cost_bias);
    if (joint.n_vars == 0) {
        printf("No variables to optimize.\n");
        return 1;
    }
    int N = joint.n_vars;

    auto stepper = std::make_unique<QUBOStepper>(joint.qubo, seed, sa_iters, sa_temp);

    // VizApp
    VizApp app("CRisky Joint Economy QUBO Viz", 1600, 900);
    app.init_camera(graph);

    // Ring buffers
    RingBuffer<float> buf_obj(16384);
    RingBuffer<float> buf_best(16384);
    RingBuffer<float> buf_cheap_prod(16384);
    RingBuffer<float> buf_exp_prod(16384);
    RingBuffer<float> buf_agree(16384);

    // Viz arrays (graph-sized)
    int nn = graph.num_nodes();
    std::vector<float> cheap_viz(nn, 0.5f);
    std::vector<float> exp_viz(nn, 0.5f);
    std::vector<float> agree_viz(nn, 0.5f);

    // Stats
    int cheap_factories = 0, exp_factories = 0, agreements = 0;
    float cheap_prod = 0.0f, exp_prod = 0.0f;
    float cheap_cost = 0.0f, exp_cost = 0.0f, transition_cost = 0.0f;
    float cost_F = static_cast<float>(config.cost_factory);
    float cost_PP = static_cast<float>(config.cost_powerplant);

    // Node state arrays (reused to avoid per-tick allocation)
    std::vector<NodeData> cheap_nodes, exp_nodes;

    auto update_viz = [&]() {
        const auto& part = stepper->best_partition();
        cheap_factories = 0;
        exp_factories = 0;
        agreements = 0;

        cheap_nodes = blank_nodes;
        exp_nodes = blank_nodes;

        for (int vi = 0; vi < N; vi++) {
            int ni = joint.var_to_node[vi];
            int c = part[vi];
            int e = part[N + vi];

            cheap_viz[ni] = (c > 0) ? 1.0f : 0.0f;
            exp_viz[ni] = (e > 0) ? 1.0f : 0.0f;
            agree_viz[ni] = (c == e) ? 1.0f : 0.0f;

            if (c > 0) cheap_factories++;
            if (e > 0) exp_factories++;
            if (c == e) agreements++;

            cheap_nodes[ni].state = (c > 0) ? NodeState::FACTORY : NodeState::POWERPLANT;
            exp_nodes[ni].state = (e > 0) ? NodeState::FACTORY : NodeState::POWERPLANT;
        }

        cheap_prod = compute_production_rate(graph, cheap_nodes, 0, config);
        exp_prod = compute_production_rate(graph, exp_nodes, 0, config);

        // Build costs
        int cheap_pps = N - cheap_factories;
        int exp_pps = N - exp_factories;
        cheap_cost = static_cast<float>(cheap_factories) * cost_F + static_cast<float>(cheap_pps) * cost_PP;
        exp_cost = static_cast<float>(exp_factories) * cost_F + static_cast<float>(exp_pps) * cost_PP;

        // Transition cost: rebuild cost at disagreement nodes
        transition_cost = 0.0f;
        for (int vi = 0; vi < N; vi++) {
            if (part[vi] != part[N + vi]) {
                // Rebuilding: pay the cost of the expensive plan's target
                transition_cost += (part[N + vi] > 0) ? cost_F : cost_PP;
            }
        }
    };
    update_viz();

    // Display game (updated each tick to show expensive plan on the graph)
    auto display_game = make_display_game(config, graph, exp_nodes);

    // Node-to-var reverse lookup (for edge coloring)
    std::vector<int> n2v(nn, -1);
    for (int vi = 0; vi < N; vi++) {
        n2v[joint.var_to_node[vi]] = vi;
    }

    // ── Panels ───────────────────────────────────────────────

    PanelHost host;
    const auto& init_scheme = COLOR_SCHEMES[app.scheme_idx()];

    // Charts in tabs
    auto charts = std::make_unique<TabbedPanel>("Charts");

    auto sa_chart_ptr = std::make_unique<TimeSeriesChart>("SA Trace", "Iteration", "Objective");
    sa_chart_ptr->add_series("Current", scheme_metric_color(init_scheme, 0), &buf_obj);
    sa_chart_ptr->add_series("Best", scheme_metric_color(init_scheme, 1), &buf_best);
    auto* sa_chart = sa_chart_ptr.get();
    charts->add_tab(std::move(sa_chart_ptr));

    auto prod_chart_ptr = std::make_unique<TimeSeriesChart>("Production", "Iteration", "Troops/tick");
    prod_chart_ptr->add_series("Cheap", 0xFF55AAFF, &buf_cheap_prod);
    prod_chart_ptr->add_series("Expensive", 0xFF55FF55, &buf_exp_prod);
    auto* prod_chart = prod_chart_ptr.get();
    charts->add_tab(std::move(prod_chart_ptr));

    auto agree_chart_ptr = std::make_unique<TimeSeriesChart>("Agreement %", "Iteration", "%");
    agree_chart_ptr->add_series("Agreement", 0xFF00DDDD, &buf_agree);
    charts->add_tab(std::move(agree_chart_ptr));

    host.add(std::move(charts));

    // Cheap plan heatmap
    auto cheap_map = std::make_unique<GraphHeatmap>(
        "Cheap Plan (F=blue, PP=orange)", &graph,
        [&]() -> std::vector<float> { return cheap_viz; }
    );
    cheap_map->set_node_color_fn([&]() -> std::vector<unsigned int> {
        std::vector<unsigned int> colors(nn, IM_COL32(100, 100, 100, 255));
        const auto& part = stepper->best_partition();
        for (int vi = 0; vi < N; vi++) {
            int ni = joint.var_to_node[vi];
            colors[ni] = (part[vi] > 0)
                ? IM_COL32(80, 140, 255, 255)    // factory = blue
                : IM_COL32(255, 160, 50, 255);   // PP = orange
        }
        colors[0] = IM_COL32(50, 255, 100, 255); // capital = green
        return colors;
    });
    cheap_map->set_edge_value_fn([&]() -> std::vector<float> {
        int ne = static_cast<int>(graph.edges.size());
        std::vector<float> ev(ne, 0.0f);
        const auto& part = stepper->best_partition();
        for (const auto& e : graph.edges) {
            int va = n2v[e.a_idx], vb = n2v[e.b_idx];
            if (va >= 0 && vb >= 0 && part[va] != part[vb])
                ev[e.idx] = 1.0f;
        }
        return ev;
    });
    host.add(std::move(cheap_map));

    // Expensive plan heatmap
    auto exp_map = std::make_unique<GraphHeatmap>(
        "Expensive Plan (F=blue, PP=orange)", &graph,
        [&]() -> std::vector<float> { return exp_viz; }
    );
    exp_map->set_node_color_fn([&]() -> std::vector<unsigned int> {
        std::vector<unsigned int> colors(nn, IM_COL32(100, 100, 100, 255));
        const auto& part = stepper->best_partition();
        for (int vi = 0; vi < N; vi++) {
            int ni = joint.var_to_node[vi];
            colors[ni] = (part[N + vi] > 0)
                ? IM_COL32(80, 140, 255, 255)
                : IM_COL32(255, 160, 50, 255);
        }
        colors[0] = IM_COL32(50, 255, 100, 255);
        return colors;
    });
    exp_map->set_edge_value_fn([&]() -> std::vector<float> {
        int ne = static_cast<int>(graph.edges.size());
        std::vector<float> ev(ne, 0.0f);
        const auto& part = stepper->best_partition();
        for (const auto& e : graph.edges) {
            int va = n2v[e.a_idx], vb = n2v[e.b_idx];
            if (va >= 0 && vb >= 0 && part[N + va] != part[N + vb])
                ev[e.idx] = 1.0f;
        }
        return ev;
    });
    host.add(std::move(exp_map));

    // Agreement heatmap
    auto agree_map = std::make_unique<GraphHeatmap>(
        "Agreement (green=agree, red=disagree)", &graph,
        [&]() -> std::vector<float> { return agree_viz; }
    );
    agree_map->set_node_color_fn([&]() -> std::vector<unsigned int> {
        std::vector<unsigned int> colors(nn, IM_COL32(100, 100, 100, 255));
        const auto& part = stepper->best_partition();
        for (int vi = 0; vi < N; vi++) {
            int ni = joint.var_to_node[vi];
            if (part[vi] == part[N + vi]) {
                colors[ni] = IM_COL32(50, 220, 80, 255);   // agree = green
            } else {
                colors[ni] = IM_COL32(220, 50, 50, 255);   // disagree = red
            }
        }
        colors[0] = IM_COL32(50, 255, 100, 255);
        return colors;
    });
    host.add(std::move(agree_map));

    // Stats table
    host.add(std::make_unique<StatsTable>("Joint QUBO Stats", [&]() {
        char buf[64];
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [&buf](const char* f, auto v) {
            std::snprintf(buf, sizeof(buf), f, v);
            return std::string(buf);
        };
        rows.push_back({"Iteration", fmt("%d", stepper->iteration())});
        rows.push_back({"QUBO Value", fmt("%.2f", stepper->best_objective())});
        rows.push_back({"", ""});
        rows.push_back({"--- Cheap ---", ""});
        rows.push_back({"  Factories", fmt("%d", cheap_factories)});
        rows.push_back({"  Powerplants", fmt("%d", N - cheap_factories)});
        rows.push_back({"  Production", fmt("%.1f", cheap_prod)});
        rows.push_back({"  Build Cost", fmt("%.0f", cheap_cost)});
        rows.push_back({"  Prod/Cost", fmt("%.4f", cheap_cost > 0 ? cheap_prod / cheap_cost : 0.0f)});
        rows.push_back({"", ""});
        rows.push_back({"--- Expensive ---", ""});
        rows.push_back({"  Factories", fmt("%d", exp_factories)});
        rows.push_back({"  Powerplants", fmt("%d", N - exp_factories)});
        rows.push_back({"  Production", fmt("%.1f", exp_prod)});
        rows.push_back({"  Build Cost", fmt("%.0f", exp_cost)});
        rows.push_back({"  Prod/Cost", fmt("%.4f", exp_cost > 0 ? exp_prod / exp_cost : 0.0f)});
        rows.push_back({"", ""});
        rows.push_back({"--- Bridge ---", ""});
        float agree_pct = N > 0 ? 100.0f * static_cast<float>(agreements) / static_cast<float>(N) : 0.0f;
        rows.push_back({"  Agreement", fmt("%.0f%%", agree_pct)});
        rows.push_back({"  Disagree", fmt("%d / %d", N - agreements)});
        rows.push_back({"  Transition Cost", fmt("%.0f", transition_cost)});
        rows.push_back({"", ""});
        rows.push_back({"mu_cheap", fmt("%.3f", mu_cheap)});
        rows.push_back({"mu_expensive", fmt("%.3f", mu_expensive)});
        rows.push_back({"mu_bridge", fmt("%.4f", mu_bridge)});
        rows.push_back({"cost_bias", fmt("%.2f", cost_bias)});
        rows.push_back({"Variables", fmt("%d (2x%d)", joint.qubo.n)});
        rows.push_back({"Graph nodes", fmt("%d", nn)});
        return rows;
    }));

    int tick_count = 0;
    host.add(std::make_unique<PlaybackControls>(&app.speed(), &app.paused(), &tick_count));

    // ── Main loop ────────────────────────────────────────────

    bool needs_rebuild = false;
    bool needs_new_graph = false;

    while (!app.should_close()) {
        // Simulation ticks
        if (!app.paused()) {
            float tick_dt;
            while ((tick_dt = app.consume_tick()) > 0) {
                int steps = std::max(1, static_cast<int>(iters_per_frame * app.speed()));
                stepper->step(steps);
                buf_obj.push(stepper->current_objective());
                buf_best.push(stepper->best_objective());
                update_viz();
                buf_cheap_prod.push(cheap_prod);
                buf_exp_prod.push(exp_prod);
                float agree_pct_buf = N > 0 ? 100.0f * static_cast<float>(agreements) / static_cast<float>(N) : 0.0f;
                buf_agree.push(agree_pct_buf);
                display_game = make_display_game(config, graph, exp_nodes);
                tick_count++;
            }
        }

        // Rendering
        app.begin_frame();
        app.draw_game(*display_game);

        // Status bar
        float agree_pct = N > 0 ? 100.0f * static_cast<float>(agreements) / static_cast<float>(N) : 0.0f;
        const char* status = app.paused() ? "  [PAUSED]" : "";
        const auto& scheme = COLOR_SCHEMES[app.scheme_idx()];
        Color status_color = scheme.sys_color();
        status_color.a = 200;
        DrawText(TextFormat("Cheap: %.1f  Expensive: %.1f  Agreement: %.0f%%  Obj: %.1f%s",
                            cheap_prod, exp_prod, agree_pct,
                            stepper->best_objective(), status),
                 10, app.screen_h() - 30, 16, status_color);

        // Theme sync
        sa_chart->set_series_color(0, scheme_metric_color(scheme, 0));
        sa_chart->set_series_color(1, scheme_metric_color(scheme, 1));

        app.begin_imgui();
        host.draw();

        // ── Controls panel ───────────────────────────────────
        if (ImGui::Begin("Controls")) {
            if (ImGui::Button("Reset SA")) {
                needs_rebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("New Graph")) {
                seed++;
                needs_new_graph = true;
            }

            ImGui::Separator();
            ImGui::Text("QUBO Weights");

            // Logarithmic sliders for all mu values (wide range)
            float log_mc = std::log10(std::max(mu_cheap, 0.01f));
            if (ImGui::SliderFloat("mu_cheap (log)", &log_mc, -2.0f, 3.0f, "10^%.1f")) {
                mu_cheap = std::pow(10.0f, log_mc);
                needs_rebuild = true;
            }
            ImGui::SameLine();
            ImGui::Text("= %.2f", mu_cheap);

            float log_me = std::log10(std::max(mu_expensive, 0.01f));
            if (ImGui::SliderFloat("mu_exp (log)", &log_me, -2.0f, 3.0f, "10^%.1f")) {
                mu_expensive = std::pow(10.0f, log_me);
                needs_rebuild = true;
            }
            ImGui::SameLine();
            ImGui::Text("= %.2f", mu_expensive);

            float log_bridge = std::log10(std::max(mu_bridge, 0.001f));
            if (ImGui::SliderFloat("mu_bridge (log)", &log_bridge, -3.0f, 2.0f, "10^%.1f")) {
                mu_bridge = std::pow(10.0f, log_bridge);
                needs_rebuild = true;
            }
            ImGui::SameLine();
            ImGui::Text("= %.4f", mu_bridge);

            // Cost bias: the main differentiator between cheap and expensive
            // Q diagonal is ~0.5, so cost_bias > 2 starts dominating adjacency
            if (ImGui::SliderFloat("cost_bias", &cost_bias, 0.0f, 20.0f))
                needs_rebuild = true;
            ImGui::SameLine();
            float eff_bias = cost_bias * (cost_PP - cost_F) / (2.0f * cost_PP);
            ImGui::Text("(eff=%.2f)", eff_bias);

            ImGui::Separator();
            ImGui::Text("SA Parameters");
            ImGui::SliderFloat("Temperature", &sa_temp, 0.1f, 20.0f);
            ImGui::SliderInt("Iters (total)", &sa_iters, 1000, 100000);
            ImGui::SliderInt("Iters/Frame", &iters_per_frame, 1, 500);

            ImGui::Separator();
            ImGui::Text("Graph");
            int seed_int = static_cast<int>(seed);
            if (ImGui::InputInt("Seed", &seed_int)) {
                seed = static_cast<uint64_t>(std::max(0, seed_int));
                needs_new_graph = true;
            }
            if (ImGui::SliderInt("Nodes", &n_nodes_hint, 10, 300))
                needs_new_graph = true;
            if (ImGui::SliderFloat("Edge Dist", &config.edge_distance_threshold, 5.0f, 50.0f))
                needs_new_graph = true;
            if (ImGui::SliderInt("Max Neighbors", &config.max_neighbors, 2, 12))
                needs_new_graph = true;
        }
        ImGui::End();

        app.end_imgui();
        app.end_frame();

        // ── Rebuild QUBO (same graph, new params) ────────────
        if (needs_rebuild && !needs_new_graph) {
            joint = qubo_objective_joint(graph, blank_nodes, 0, config,
                                         mu_cheap, mu_expensive, mu_bridge, cost_bias);
            N = joint.n_vars;
            stepper = std::make_unique<QUBOStepper>(joint.qubo, seed, sa_iters, sa_temp);
            buf_obj = RingBuffer<float>(16384);
            buf_best = RingBuffer<float>(16384);
            buf_cheap_prod = RingBuffer<float>(16384);
            buf_exp_prod = RingBuffer<float>(16384);
            buf_agree = RingBuffer<float>(16384);
            update_viz();
            display_game = make_display_game(config, graph, exp_nodes);
            tick_count = 0;
            needs_rebuild = false;
        }

        // ── New graph ────────────────────────────────────────
        if (needs_new_graph) {
            gen_config = config;
            if (n_nodes_hint > 0) {
                float area = gen_config.region_width * gen_config.region_height;
                gen_config.poisson_intensity = static_cast<float>(n_nodes_hint) / area;
            }
            Game new_game(gen_config, {0}, seed);
            graph = new_game.graph();
            nn = graph.num_nodes();
            blank_nodes = make_blank_nodes(graph, config);

            joint = qubo_objective_joint(graph, blank_nodes, 0, config,
                                         mu_cheap, mu_expensive, mu_bridge, cost_bias);
            N = joint.n_vars;
            stepper = std::make_unique<QUBOStepper>(joint.qubo, seed, sa_iters, sa_temp);

            // Rebuild reverse lookup
            n2v.assign(nn, -1);
            for (int vi = 0; vi < N; vi++) {
                n2v[joint.var_to_node[vi]] = vi;
            }

            // Reset viz arrays
            cheap_viz.assign(nn, 0.5f);
            exp_viz.assign(nn, 0.5f);
            agree_viz.assign(nn, 0.5f);

            buf_obj = RingBuffer<float>(16384);
            buf_best = RingBuffer<float>(16384);
            buf_cheap_prod = RingBuffer<float>(16384);
            buf_exp_prod = RingBuffer<float>(16384);
            buf_agree = RingBuffer<float>(16384);
            update_viz();
            display_game = make_display_game(config, graph, exp_nodes);
            app.init_camera(graph);
            tick_count = 0;
            needs_new_graph = false;
            needs_rebuild = false;
        }
    }

    float agree_pct = N > 0 ? 100.0f * static_cast<float>(agreements) / static_cast<float>(N) : 0.0f;
    printf("Final: obj=%.2f, cheap_prod=%.1f, exp_prod=%.1f, agreement=%.0f%%\n",
           stepper->best_objective(), cheap_prod, exp_prod, agree_pct);
    return 0;
}
