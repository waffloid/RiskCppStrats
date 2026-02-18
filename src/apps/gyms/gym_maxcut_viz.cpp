#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/game.hpp"
#include "engine/game_config.hpp"
#include "systems/graph_algo/maxcut_solvers.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "systems/ordering/ordering_solvers.hpp"
#include "gyms/economy_gym.hpp"

#include "viz/viz_app.hpp"
#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"
#include "viz/panels/tabbed_panel.hpp"

static std::string get_arg(int argc, char** argv, const std::string& prefix,
                            const std::string& default_val) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find(prefix) == 0) return arg.substr(prefix.size());
    }
    return default_val;
}

int main(int argc, char** argv) {
    std::string objective_name = get_arg(argc, argv, "--objective=", "production");
    uint64_t seed = static_cast<uint64_t>(std::atoi(get_arg(argc, argv, "--seed=", "42").c_str()));
    int nodes_hint = std::atoi(get_arg(argc, argv, "--nodes=", "100").c_str());
    int sa_iters = std::atoi(get_arg(argc, argv, "--iters=", "10000").c_str());
    float sa_temp = static_cast<float>(std::atof(get_arg(argc, argv, "--temp=", "5.0").c_str()));

    // Generate economy state (graph + owned nodes)
    GameConfig config{};
    GameConfig gen_config = config;
    if (nodes_hint > 0) {
        float area = gen_config.region_width * gen_config.region_height;
        gen_config.poisson_intensity = static_cast<float>(nodes_hint) / area;
    }
    Game game(gen_config, {0}, seed);
    Graph graph = game.graph();
    std::vector<NodeData> nodes = game.node_data();
    for (int i = 0; i < graph.num_nodes(); i++) {
        nodes[i].owner = 0;
        if (nodes[i].troops.empty()) nodes[i].troops.resize(2, 0);
        nodes[i].troops[0] = config.cost_powerplant + 1;
    }
    nodes[0].state = NodeState::CAPITAL;

    // Build QUBO
    auto builder = get_qubo_objective(objective_name);
    if (!builder) {
        printf("Unknown objective: %s\n", objective_name.c_str());
        return 1;
    }
    auto eco_inst = builder(graph, nodes, 0, config);
    if (eco_inst.qubo.n == 0) {
        printf("No variables to optimize.\n");
        return 1;
    }

    // Create stepper
    QUBOStepper stepper(eco_inst.qubo, seed, sa_iters, sa_temp);

    // Create VizApp
    VizApp app("CRisky Max-Cut Gym Viz", 1400, 800);
    // Set up camera from a dummy game for the graph
    Game dummy_game(gen_config, {0}, seed);
    app.init_camera(dummy_game.graph());

    // Ring buffers for SA trace
    RingBuffer<float> buf_obj(16384);
    RingBuffer<float> buf_best(16384);

    int tick_count = 0;
    int iters_per_tick = 50;

    // Current partition mapped to full graph (for viz)
    std::vector<float> partition_viz(graph.num_nodes(), 0.5f);
    std::vector<float> flip_gain_viz(graph.num_nodes(), 0.0f);

    auto update_viz = [&]() {
        const auto& part = stepper.best_partition();
        const auto& gains = stepper.flip_gains();
        for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
            int ni = eco_inst.var_to_node[vi];
            partition_viz[ni] = (part[vi] > 0) ? 1.0f : 0.0f;
            flip_gain_viz[ni] = gains[vi];
        }
    };
    update_viz();

    // Panel host
    PanelHost host;

    // Charts tab
    auto charts = std::make_unique<TabbedPanel>("Charts");
    auto sa_chart = std::make_unique<TimeSeriesChart>("SA Trace", "Iteration", "Objective");
    sa_chart->add_series("Current", 0xFF00AAFF, &buf_obj);
    sa_chart->add_series("Best", 0xFF00FF00, &buf_best);
    charts->add_tab(std::move(sa_chart));
    host.add(std::move(charts));

    // Partition heatmap with edge coloring and direct node colors
    auto partition_map = std::make_unique<GraphHeatmap>(
        "Partition (Factory=blue, PP=orange)", &graph,
        [&]() -> std::vector<float> { return partition_viz; }
    );
    partition_map->set_node_color_fn([&]() -> std::vector<unsigned int> {
        int nn = graph.num_nodes();
        std::vector<unsigned int> colors(nn, IM_COL32(100, 100, 100, 255));
        const auto& part = stepper.best_partition();
        for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
            int ni = eco_inst.var_to_node[vi];
            if (part[vi] > 0) {
                colors[ni] = IM_COL32(80, 140, 255, 255);   // factory = blue
            } else {
                colors[ni] = IM_COL32(255, 160, 50, 255);   // powerplant = orange
            }
        }
        // Capital = green
        colors[0] = IM_COL32(50, 255, 100, 255);
        return colors;
    });
    partition_map->set_edge_value_fn([&]() -> std::vector<float> {
        int ne = static_cast<int>(graph.edges.size());
        std::vector<float> ev(ne, 0.0f);
        const auto& part = stepper.best_partition();
        // Build node-to-var lookup
        std::vector<int> n2v(graph.num_nodes(), -1);
        for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
            n2v[eco_inst.var_to_node[vi]] = vi;
        }
        for (const auto& e : graph.edges) {
            int va = n2v[e.a_idx], vb = n2v[e.b_idx];
            if (va >= 0 && vb >= 0 && part[va] != part[vb]) {
                ev[e.idx] = 1.0f;  // cut edge = bright
            }
        }
        return ev;
    });
    host.add(std::move(partition_map));

    // Flip gain heatmap
    host.add(std::make_unique<GraphHeatmap>(
        "Flip Gain", &graph,
        [&]() -> std::vector<float> { return flip_gain_viz; }
    ));

    // Stats table
    host.add(std::make_unique<StatsTable>("Stats", [&]() {
        // Count factories and powerplants from best partition
        int nf = 0, npp = 0;
        const auto& part = stepper.best_partition();
        for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
            if (part[vi] > 0) nf++; else npp++;
        }
        // Compute actual production
        std::vector<NodeData> work = nodes;
        for (int vi = 0; vi < eco_inst.qubo.n; vi++) {
            int ni = eco_inst.var_to_node[vi];
            work[ni].state = (part[vi] > 0) ? NodeState::FACTORY : NodeState::POWERPLANT;
        }
        float prod = compute_production_rate(graph, work, 0, config);
        float theo = compute_theoretical_max_production(graph, work, 0, config);

        char buf[64];
        std::vector<std::pair<std::string, std::string>> rows;
        rows.push_back({"Objective", objective_name});
        snprintf(buf, sizeof(buf), "%d", stepper.iteration());
        rows.push_back({"Iteration", buf});
        snprintf(buf, sizeof(buf), "%.2f", stepper.best_objective());
        rows.push_back({"QUBO Value", buf});
        snprintf(buf, sizeof(buf), "%.1f", prod);
        rows.push_back({"Production", buf});
        snprintf(buf, sizeof(buf), "%.1f%%", (theo > 0 ? prod / theo * 100.0f : 0.0f));
        rows.push_back({"Efficiency", buf});
        snprintf(buf, sizeof(buf), "%d / %d", nf, npp);
        rows.push_back({"F / PP", buf});
        snprintf(buf, sizeof(buf), "%d", graph.num_nodes());
        rows.push_back({"Nodes", buf});
        snprintf(buf, sizeof(buf), "%d", eco_inst.qubo.n);
        rows.push_back({"Variables", buf});
        return rows;
    }));

    // Playback controls
    host.add(std::make_unique<PlaybackControls>(&app.speed(), &app.paused(), &tick_count));

    // Main loop
    while (!app.should_close()) {
        float tick_dt;
        while ((tick_dt = app.consume_tick()) > 0) {
            stepper.step(iters_per_tick);
            buf_obj.push(stepper.current_objective());
            buf_best.push(stepper.best_objective());
            update_viz();
            tick_count++;
        }

        app.begin_frame();
        app.draw_game(dummy_game);

        app.begin_imgui();
        host.draw();
        app.end_imgui();

        app.end_frame();
    }

    printf("Final: obj=%.2f, iter=%d\n", stepper.best_objective(), stepper.iteration());
    return 0;
}
