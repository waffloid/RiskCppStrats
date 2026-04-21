#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/potential_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "systems/transport/ot_solver.hpp"
#include "engine/graph_builder.hpp"
#include "engine/game.hpp"

#include <cmath>
#include <functional>
#include <map>
#include <random>

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

// --- Poisson graph helper (matches crisky_game config) ---

static Graph make_poisson_graph(uint64_t seed = 42) {
    GameConfig config{};
    config.poisson_intensity = 0.16f;
    config.region_width = 158.0f;
    config.region_height = 158.0f;
    config.edge_distance_threshold = 10.0f;
    config.max_neighbors = 7;
    config.circular = true;
    config.num_holes = 6;
    config.hole_radius_min = 8.0f;
    config.hole_radius_max = 18.0f;
    return Graph::generate_poisson(config, seed);
}

// --- Poisson presets ---

static TransportPreset make_poisson_edge_ring() {
    TransportPreset p;
    p.name = "poisson_edge_ring";
    p.graph = make_poisson_graph();
    int n = p.graph.num_nodes();

    int troops_per_node = 1000;
    int total = troops_per_node * n;
    p.initial_troops.assign(n, troops_per_node);

    // Compute graph center
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++) {
        cx += p.graph.nodes[i].x;
        cy += p.graph.nodes[i].y;
    }
    cx /= n;
    cy /= n;

    // Target weight = distance from center
    std::vector<float> weights(n);
    float weight_sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = p.graph.nodes[i].x - cx;
        float dy = p.graph.nodes[i].y - cy;
        weights[i] = std::sqrt(dx * dx + dy * dy);
        weight_sum += weights[i];
    }

    // Normalize to conserve total troops
    p.target_troops.assign(n, 0);
    int assigned = 0;
    for (int i = 0; i < n; i++) {
        p.target_troops[i] = static_cast<int>(weights[i] / weight_sum * total);
        assigned += p.target_troops[i];
    }
    // Distribute rounding remainder
    int remainder = total - assigned;
    for (int i = 0; i < remainder; i++) p.target_troops[i % n]++;

    return p;
}

static TransportPreset make_poisson_3_clusters() {
    TransportPreset p;
    p.name = "poisson_3_clusters";
    p.graph = make_poisson_graph();
    int n = p.graph.num_nodes();

    int troops_per_node = 1000;
    int total = troops_per_node * n;
    p.initial_troops.assign(n, troops_per_node);

    // Graph center
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++) {
        cx += p.graph.nodes[i].x;
        cy += p.graph.nodes[i].y;
    }
    cx /= n;
    cy /= n;

    // Find max radius for boundary scan
    float max_r = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = p.graph.nodes[i].x - cx;
        float dy = p.graph.nodes[i].y - cy;
        float r = std::sqrt(dx * dx + dy * dy);
        if (r > max_r) max_r = r;
    }

    // Pick 3 seed nodes closest to 120 degree positions on boundary
    constexpr float pi = 3.14159265358979f;
    float target_angles[3] = {0.0f, 2.0f * pi / 3.0f, 4.0f * pi / 3.0f};
    int seeds[3] = {0, 0, 0};

    for (int s = 0; s < 3; s++) {
        float tx = cx + max_r * std::cos(target_angles[s]);
        float ty = cy + max_r * std::sin(target_angles[s]);
        float best_dist = 1e18f;
        for (int i = 0; i < n; i++) {
            float dx = p.graph.nodes[i].x - tx;
            float dy = p.graph.nodes[i].y - ty;
            float d = dx * dx + dy * dy;
            if (d < best_dist) {
                best_dist = d;
                seeds[s] = i;
            }
        }
    }

    // Target weight = 1/(1+d^2) to nearest seed
    std::vector<float> weights(n);
    float weight_sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float best = 1e18f;
        for (int s = 0; s < 3; s++) {
            float dx = p.graph.nodes[i].x - p.graph.nodes[seeds[s]].x;
            float dy = p.graph.nodes[i].y - p.graph.nodes[seeds[s]].y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) best = d2;
        }
        weights[i] = 1.0f / (1.0f + best);
        weight_sum += weights[i];
    }

    // Normalize
    p.target_troops.assign(n, 0);
    int assigned = 0;
    for (int i = 0; i < n; i++) {
        p.target_troops[i] = static_cast<int>(weights[i] / weight_sum * total);
        assigned += p.target_troops[i];
    }
    int remainder = total - assigned;
    for (int i = 0; i < remainder; i++) p.target_troops[i % n]++;

    return p;
}

static TransportPreset make_poisson_capital_rush() {
    TransportPreset p;
    p.name = "poisson_capital_rush";
    p.graph = make_poisson_graph();
    int n = p.graph.num_nodes();

    int troops_per_node = 1000;
    int total = troops_per_node * n;
    p.initial_troops.assign(n, troops_per_node);

    // All troops to node 0
    p.target_troops.assign(n, 0);
    p.target_troops[0] = total;

    return p;
}

// --- Registry ---

using PresetFactory = std::function<TransportPreset()>;

static const std::map<std::string, PresetFactory>& preset_registry() {
    static const std::map<std::string, PresetFactory> presets = {
        {"star_center",           make_star_center},
        {"path_end",              make_path_end},
        {"bipartite_split",       make_bipartite_split},
        {"poisson_edge_ring",     make_poisson_edge_ring},
        {"poisson_3_clusters",    make_poisson_3_clusters},
        {"poisson_capital_rush",  make_poisson_capital_rush},
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

// --- Shared per-tick simulation ---

TransportTickResult transport_gym_tick(
    Game& game,
    const TransportPreset& preset,
    const TransportSolver& solver,
    const LossFunction& loss_fn,
    std::mt19937& rng,
    std::vector<float>& warm_phi,
    float dt) {

    int n = game.graph().num_nodes();
    TransportTickResult result;

    // Distribution: on-node + in-transit counted at origin.
    // Troops going A→B count as "at A" until they arrive (idempotency).
    result.current.resize(n);
    for (int i = 0; i < n; i++) {
        result.current[i] = game.node_data()[i].troops[0];
    }

    // Per-node: total troops already committed outbound from this node.
    // Retreating troops are heading home — count them at origin for distribution
    // but NOT as committed outflow (they're no longer satisfying a send).
    std::vector<int> committed(n, 0);
    result.in_transit = 0;
    for (const auto& el : game.edge_lanes()) {
        for (int lane = 0; lane < 2; lane++) {
            int origin = (lane == 0) ? el.node_a : el.node_b;
            for (const auto& g : el.lanes[lane].groups) {
                if (g.owner != 0) continue;
                result.in_transit += g.count;
                result.current[origin] += g.count;
                if (!g.retreating) {
                    committed[origin] += g.count;
                }
            }
        }
    }

    // Loss
    result.loss = loss_fn(result.current, preset.target_troops);

    // Compute deficit, then solve L*phi = deficit for the true potential
    result.deficit.resize(n);
    for (int i = 0; i < n; i++) {
        result.deficit[i] = static_cast<float>(preset.target_troops[i] - result.current[i]);
    }
    solve_graph_poisson(game.graph(), result.deficit, warm_phi);
    result.gradient = warm_phi;

    // Build effective node data with in-transit troops counted at origin.
    // This makes the solver see idempotent troop counts (on-node + in-transit).
    std::vector<NodeData> effective_nodes = game.node_data();
    for (int i = 0; i < n; i++) {
        effective_nodes[i].troops[0] = result.current[i];
    }

    // Get commands from solver
    std::vector<bool> no_mask(n, false);
    auto commands = solver(game.graph(), effective_nodes, result.gradient, 0, no_mask);

    // Discount already-committed in-transit troops from each node's send budget.
    std::vector<int> sendable(n);
    for (int i = 0; i < n; i++) {
        int surplus = result.current[i] - preset.target_troops[i];
        sendable[i] = std::max(0, surplus - committed[i]);
    }
    for (auto& cmd : commands) {
        if (sendable[cmd.from_node] <= 0) {
            cmd.count = 0;
        } else {
            if (cmd.count > sendable[cmd.from_node])
                cmd.count = sendable[cmd.from_node];
            sendable[cmd.from_node] -= cmd.count;
        }
    }
    commands.erase(
        std::remove_if(commands.begin(), commands.end(),
            [](const TroopCommand& c) { return c.count <= 0; }),
        commands.end());

    // Stochastic 1-troop sends for nodes with small fractional surplus.
    std::uniform_real_distribution<float> coin(0.0f, 1.0f);
    for (int i = 0; i < n; i++) {
        if (sendable[i] <= 0) continue;
        if (game.node_data()[i].troops[0] < 2) continue;
        float prob = static_cast<float>(sendable[i]) /
                     static_cast<float>(std::max(1, preset.target_troops[i]));
        if (coin(rng) >= prob) continue;
        int best_nb = -1;
        float best_grad = -1e18f;
        for (int nb : game.graph().neighbors(i)) {
            if (result.gradient[nb] > best_grad) {
                best_grad = result.gradient[nb];
                best_nb = nb;
            }
        }
        if (best_nb >= 0 && best_grad > result.gradient[i]) {
            commands.push_back({i, best_nb, 1});
        }
    }

    // Apply via game tick
    std::vector<PlayerCommands> all_commands(game.n_players());
    all_commands[0].troops = std::move(commands);
    game.tick(dt, all_commands);

    return result;
}

// --- Gym runner ---

TransportGymResult run_transport_gym(
    const TransportPreset& preset,
    const std::string& solver_name,
    const std::string& loss_name,
    int max_ticks) {

    TransportGymResult result;
    result.total_ticks = max_ticks;

    TransportSolver solver;
    if (solver_name == "ot") {
        solver = make_ot_solver(preset.graph, preset.target_troops);
    } else {
        solver = get_transport_solver(solver_name);
    }
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

    std::mt19937 rng(42);
    std::vector<float> warm_phi;
    float prev_loss = 0.0f;

    for (int tick = 0; tick < max_ticks; tick++) {
        auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);

        if (tick == 0) {
            result.initial_loss = tr.loss;
            prev_loss = tr.loss;
        }

        result.ticks.push_back({tr.loss, tr.loss - prev_loss, tr.in_transit});
        prev_loss = tr.loss;

        if (tr.loss < 1.0f && result.ticks_to_converge == 0) {
            result.ticks_to_converge = tick;
        }
    }

    result.final_loss = result.ticks.empty() ? 0.0f : result.ticks.back().loss;
    return result;
}
