#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/graph_builder.hpp"
#include "engine/game.hpp"

#include <functional>
#include <map>

// --- Preset definitions ---

static TransportPreset make_star_center() {
    TransportPreset p;
    p.name = "star_center";
    p.graph = build_star(10);
    int n = p.graph.num_nodes();  // 11: center + 10 leaves

    // All 1000 troops at center, target = uniform (~91 each)
    int total = 1000;
    int per_node = total / n;
    p.initial_troops.assign(n, 0);
    p.initial_troops[0] = total;

    p.target_troops.assign(n, per_node);
    // Distribute remainder to first nodes
    int remainder = total - per_node * n;
    for (int i = 0; i < remainder; i++) p.target_troops[i]++;

    return p;
}

static TransportPreset make_path_end() {
    TransportPreset p;
    p.name = "path_end";
    p.graph = build_path(10);
    int n = p.graph.num_nodes();

    // All 1000 troops at node 0, target = uniform
    int total = 1000;
    int per_node = total / n;
    p.initial_troops.assign(n, 0);
    p.initial_troops[0] = total;

    p.target_troops.assign(n, per_node);
    int remainder = total - per_node * n;
    for (int i = 0; i < remainder; i++) p.target_troops[i]++;

    return p;
}

static TransportPreset make_bipartite_split() {
    TransportPreset p;
    p.name = "bipartite_split";
    p.graph = build_bipartite(3, 5);
    int n = p.graph.num_nodes();  // 8

    // 1000 troops spread across left partition (nodes 0-2), target = uniform
    int total = 1000;
    p.initial_troops.assign(n, 0);
    p.initial_troops[0] = 400;
    p.initial_troops[1] = 300;
    p.initial_troops[2] = 300;

    int per_node = total / n;
    p.target_troops.assign(n, per_node);
    int remainder = total - per_node * n;
    for (int i = 0; i < remainder; i++) p.target_troops[i]++;

    return p;
}

// --- Registry ---

using PresetFactory = std::function<TransportPreset()>;

static const std::map<std::string, PresetFactory>& preset_registry() {
    static const std::map<std::string, PresetFactory> presets = {
        {"star_center",     make_star_center},
        {"path_end",        make_path_end},
        {"bipartite_split", make_bipartite_split},
    };
    return presets;
}

std::optional<TransportPreset> get_transport_preset(const std::string& name) {
    const auto& reg = preset_registry();
    auto it = reg.find(name);
    if (it == reg.end()) return std::nullopt;
    return it->second();
}

std::vector<std::string> list_transport_presets() {
    std::vector<std::string> names;
    for (const auto& [name, _] : preset_registry()) names.push_back(name);
    return names;
}

// --- Gym runner ---

TransportGymResult run_transport_gym(
    const TransportPreset& preset,
    const std::string& solver_name,
    const std::string& loss_name,
    int max_ticks) {

    TransportGymResult result;
    result.total_ticks = max_ticks;

    TransportSolver solver = get_transport_solver(solver_name);
    LossFunction loss_fn = get_loss_function(loss_name);
    if (!solver || !loss_fn) return result;

    // Set up single-player game with zero production
    GameConfig config;
    config.init_troop_count = 0;
    config.capital_troops_per_tick = 0;
    config.factory_troops_per_tick = 0;
    config.powerplant_bonus = 0;

    Game game(config, preset.graph, {0});

    // Set initial troop distribution (player 0 owns all nodes)
    int n = preset.graph.num_nodes();
    for (int i = 0; i < n; i++) {
        NodeState state = (i == 0) ? NodeState::CAPITAL : NodeState::DEFAULT;
        game.set_node_state(i, state, 0, preset.initial_troops[i]);
    }

    std::vector<bool> no_mask(n, false);
    float prev_loss = 0.0f;

    for (int tick = 0; tick < max_ticks; tick++) {
        // Current on-node distribution
        std::vector<int> current(n);
        for (int i = 0; i < n; i++) {
            current[i] = game.node_data()[i].troops[0];
        }

        // Compute loss
        float loss = loss_fn(current, preset.target_troops);
        if (tick == 0) {
            result.initial_loss = loss;
            prev_loss = loss;
        }

        // Count troops in transit
        int in_transit = 0;
        for (const auto& el : game.edge_lanes()) {
            for (int lane = 0; lane < 2; lane++) {
                for (const auto& g : el.lanes[lane].groups) {
                    if (g.owner == 0) in_transit += g.count;
                }
            }
        }

        result.ticks.push_back({loss, loss - prev_loss, in_transit});
        prev_loss = loss;

        // Check convergence (loss < 1.0)
        if (loss < 1.0f && result.ticks_to_converge == 0) {
            result.ticks_to_converge = tick;
        }

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
    }

    result.final_loss = result.ticks.empty() ? 0.0f : result.ticks.back().loss;
    return result;
}
