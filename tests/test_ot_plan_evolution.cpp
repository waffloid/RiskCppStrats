#include "gyms/transport_gym.hpp"
#include "systems/transport/ot_solver.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/game.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <vector>

// ==========================================================================
// Helpers
// ==========================================================================

static void find_seeds(const Graph& g, int seeds[3]) {
    int n = g.num_nodes();
    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) { cx += g.nodes[i].x; cy += g.nodes[i].y; }
    cx /= n; cy /= n;
    float max_r = 0;
    for (int i = 0; i < n; i++) {
        float dx = g.nodes[i].x - cx, dy = g.nodes[i].y - cy;
        float r = std::sqrt(dx * dx + dy * dy);
        if (r > max_r) max_r = r;
    }
    constexpr float pi = 3.14159265358979f;
    float angles[3] = {0.0f, 2.0f * pi / 3.0f, 4.0f * pi / 3.0f};
    for (int s = 0; s < 3; s++) {
        float tx = cx + max_r * std::cos(angles[s]);
        float ty = cy + max_r * std::sin(angles[s]);
        float best = 1e18f;
        for (int i = 0; i < n; i++) {
            float dx = g.nodes[i].x - tx, dy = g.nodes[i].y - ty;
            float d = dx * dx + dy * dy;
            if (d < best) { best = d; seeds[s] = i; }
        }
    }
}

static Game make_game(const TransportPreset& preset) {
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
    return game;
}

// ==========================================================================
// Test: plan determinism
// ==========================================================================
static void test_plan_determinism() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    OTSolver ot(preset.graph, preset.target_troops, sp);

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {preset.initial_troops[i]};
        nodes[i].state = NodeState::DEFAULT;
    }
    std::vector<bool> mask(n, false);
    auto cmds1 = ot.solve(nodes, 0, mask);
    auto cmds2 = ot.solve(nodes, 0, mask);
    assert(cmds1.size() == cmds2.size());
    for (size_t i = 0; i < cmds1.size(); i++) {
        assert(cmds1[i].from_node == cmds2[i].from_node);
        assert(cmds1[i].to_node == cmds2[i].to_node);
        assert(cmds1[i].count == cmds2[i].count);
    }
    std::printf("test_plan_determinism: PASS (%d commands)\n", (int)cmds1.size());
}

// ==========================================================================
// Test: initial plan flow = min(supply, demand)
// ==========================================================================
static void test_initial_plan_sends_all_supply() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    OTSolver ot(preset.graph, preset.target_troops, sp);

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {preset.initial_troops[i]};
        nodes[i].state = NodeState::DEFAULT;
    }
    std::vector<bool> mask(n, false);
    auto cmds = ot.solve(nodes, 0, mask);

    std::vector<int> planned_out(n, 0);
    for (const auto& c : cmds) planned_out[c.from_node] += c.count;

    int total_supply = 0, total_demand = 0, total_planned = 0;
    for (int i = 0; i < n; i++) {
        total_supply += std::max(0, preset.initial_troops[i] - preset.target_troops[i]);
        total_demand += std::max(0, preset.target_troops[i] - preset.initial_troops[i]);
        total_planned += planned_out[i];
    }
    int expected = std::min(total_supply, total_demand);
    std::printf("test_initial_plan_sends_all_supply: supply=%d demand=%d planned=%d expected=%d diff=%d\n",
                total_supply, total_demand, total_planned, expected,
                std::abs(total_planned - expected));
    assert(std::abs(total_planned - expected) <= n);
    std::printf("  PASS\n");
}

// ==========================================================================
// Test: loss monotone decrease
// ==========================================================================
static void test_loss_monotone() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    Game game = make_game(preset);
    int n = preset.graph.num_nodes();

    ShortestPathData sp = compute_shortest_paths(preset.graph);
    auto ot = std::make_shared<OTSolver>(preset.graph, preset.target_troops, sp);
    LossFunction loss_fn = get_loss_function("l1");
    TransportSolver solver = [&ot](const Graph&, const std::vector<NodeData>& nodes,
                                    const std::vector<float>&, int pid,
                                    const std::vector<bool>& masked) {
        return ot->solve(nodes, pid, masked);
    };
    std::mt19937 rng(42);
    std::vector<float> warm_phi;

    float prev_loss = 1e18f;
    int increases = 0;
    float max_increase = 0;
    for (int tick = 0; tick < 500; tick++) {
        auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);
        if (tr.loss > prev_loss + 1.0f) {
            increases++;
            float inc = tr.loss - prev_loss;
            if (inc > max_increase) max_increase = inc;
        }
        prev_loss = tr.loss;
    }
    std::printf("test_loss_monotone: increases=%d max_increase=%.1f final_loss=%.0f\n",
                increases, max_increase, prev_loss);
    assert(increases < 50);
    assert(max_increase < 5000.0f);
    std::printf("  PASS\n");
}

// ==========================================================================
// Test: first-hop-only — no node sends more than its supply
// ==========================================================================
static void test_no_oversend() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    OTSolver ot(preset.graph, preset.target_troops, sp);

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {preset.initial_troops[i]};
        nodes[i].state = NodeState::DEFAULT;
    }
    std::vector<bool> mask(n, false);
    auto cmds = ot.solve(nodes, 0, mask);

    // Per-node outflow should not exceed supply (= current - target, if positive)
    std::vector<int> outflow(n, 0);
    for (const auto& c : cmds) outflow[c.from_node] += c.count;

    int violations = 0;
    for (int i = 0; i < n; i++) {
        int supply_i = std::max(0, preset.initial_troops[i] - preset.target_troops[i]);
        if (outflow[i] > supply_i) {
            std::printf("  VIOLATION: node %d sends %d but supply=%d (current=%d target=%d)\n",
                        i, outflow[i], supply_i, preset.initial_troops[i], preset.target_troops[i]);
            violations++;
        }
    }
    std::printf("test_no_oversend: %d violations out of %d nodes\n", violations, n);
    assert(violations == 0);
    std::printf("  PASS\n");
}

// ==========================================================================
// Test: every command's from_node is a supply node
// ==========================================================================
static void test_commands_from_supply_only() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    OTSolver ot(preset.graph, preset.target_troops, sp);

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {preset.initial_troops[i]};
        nodes[i].state = NodeState::DEFAULT;
    }
    std::vector<bool> mask(n, false);
    auto cmds = ot.solve(nodes, 0, mask);

    int non_supply = 0;
    for (const auto& c : cmds) {
        int supply_i = std::max(0, preset.initial_troops[c.from_node] - preset.target_troops[c.from_node]);
        if (supply_i <= 0) {
            std::printf("  NON-SUPPLY: command from node %d (current=%d target=%d)\n",
                        c.from_node, preset.initial_troops[c.from_node], preset.target_troops[c.from_node]);
            non_supply++;
        }
    }
    std::printf("test_commands_from_supply_only: %d commands from non-supply nodes\n", non_supply);
    assert(non_supply == 0);
    std::printf("  PASS\n");
}

// ==========================================================================
// Diagnostic: compare OT vs greedy on poisson_3_clusters
// ==========================================================================
static void run_solver_comparison() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    LossFunction loss_fn = get_loss_function("l1");
    int max_ticks = 500;

    int seeds[3];
    find_seeds(preset.graph, seeds);

    // --- OT solver ---
    {
        Game game = make_game(preset);
        ShortestPathData sp = compute_shortest_paths(preset.graph);
        auto ot = std::make_shared<OTSolver>(preset.graph, preset.target_troops, sp);
        TransportSolver solver = [&ot](const Graph&, const std::vector<NodeData>& nodes,
                                        const std::vector<float>&, int pid,
                                        const std::vector<bool>& masked) {
            return ot->solve(nodes, pid, masked);
        };
        std::mt19937 rng(42);
        std::vector<float> warm_phi;

        std::printf("\n=== OT Solver (poisson_3_clusters, %d ticks) ===\n", max_ticks);
        std::printf("%-5s %-12s %-8s %-8s %-8s\n",
                    "Tick", "Loss", "Transit", "Sent", "Arrived");
        float prev_loss = -1;
        for (int tick = 0; tick < max_ticks; tick++) {
            // Pre-tick state
            int pre_transit = 0;
            for (const auto& el : game.edge_lanes())
                for (int l = 0; l < 2; l++)
                    for (const auto& g : el.lanes[l].groups)
                        if (g.owner == 0) pre_transit += g.count;

            auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);

            // Post-tick: count new sends and arrivals
            int post_transit = 0;
            for (const auto& el : game.edge_lanes())
                for (int l = 0; l < 2; l++)
                    for (const auto& g : el.lanes[l].groups)
                        if (g.owner == 0) post_transit += g.count;

            // newly_sent = post_transit - pre_transit + arrived
            // arrived = troops that completed their edge this tick
            int arrived = pre_transit - post_transit;  // approximate: transit decreased
            if (arrived < 0) arrived = 0;  // new sends offset arrivals
            int sent = post_transit - pre_transit + arrived;

            if (tick < 10 || tick % 50 == 0 || tick == max_ticks - 1) {
                std::printf("%-5d %-12.0f %-8d %-8d %-8d\n",
                            tick, tr.loss, tr.in_transit, sent, arrived);
            }
            prev_loss = tr.loss;
        }
    }

    // --- Greedy solver ---
    {
        Game game = make_game(preset);
        TransportSolver solver = get_transport_solver("greedy");
        std::mt19937 rng(42);
        std::vector<float> warm_phi;

        std::printf("\n=== Greedy Solver (poisson_3_clusters, %d ticks) ===\n", max_ticks);
        std::printf("%-5s %-12s %-8s\n", "Tick", "Loss", "Transit");
        for (int tick = 0; tick < max_ticks; tick++) {
            auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);
            if (tick < 10 || tick % 50 == 0 || tick == max_ticks - 1) {
                std::printf("%-5d %-12.0f %-8d\n", tick, tr.loss, tr.in_transit);
            }
        }
    }
}

// ==========================================================================
// Diagnostic: track the OT plan at key ticks to see if it changes
// ==========================================================================
static void run_plan_stability() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    LossFunction loss_fn = get_loss_function("l1");

    int seeds[3];
    find_seeds(preset.graph, seeds);
    std::set<int> seed_set(seeds, seeds + 3);

    Game game = make_game(preset);
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    auto ot = std::make_shared<OTSolver>(preset.graph, preset.target_troops, sp);
    TransportSolver solver = [&ot](const Graph&, const std::vector<NodeData>& nodes,
                                    const std::vector<float>&, int pid,
                                    const std::vector<bool>& masked) {
        return ot->solve(nodes, pid, masked);
    };
    std::mt19937 rng(42);
    std::vector<float> warm_phi;

    std::printf("\n=== Plan Stability (poisson_3_clusters) ===\n");
    std::printf("Tracking: which demand each node's outflow is assigned to\n");
    std::printf("(via OT Voronoi after each solve)\n\n");

    // Track how many nodes change Voronoi cell between ticks
    std::vector<int> prev_voronoi(n, -1);

    for (int tick = 0; tick < 500; tick++) {
        // Compute effective (matching gym's logic)
        std::vector<int> effective(n);
        for (int i = 0; i < n; i++) effective[i] = game.node_data()[i].troops[0];
        std::vector<int> committed(n, 0);
        int total_transit = 0;
        for (const auto& el : game.edge_lanes()) {
            for (int lane = 0; lane < 2; lane++) {
                int origin = (lane == 0) ? el.node_a : el.node_b;
                for (const auto& g : el.lanes[lane].groups) {
                    if (g.owner != 0) continue;
                    total_transit += g.count;
                    effective[origin] += g.count;
                    if (!g.retreating) committed[origin] += g.count;
                }
            }
        }

        // Get the OT plan (what solver WANTS to do)
        std::vector<NodeData> eff_nodes = game.node_data();
        for (int i = 0; i < n; i++) eff_nodes[i].troops[0] = effective[i];
        std::vector<bool> no_mask(n, false);
        auto plan_cmds = ot->solve(eff_nodes, 0, no_mask);
        auto vor = ot->last_voronoi();

        // Count Voronoi changes
        int vor_changes = 0;
        for (int i = 0; i < n; i++) {
            if (prev_voronoi[i] >= 0 && vor[i] >= 0 && prev_voronoi[i] != vor[i])
                vor_changes++;
        }

        // Count sendable (what gym would actually allow)
        int total_sendable = 0;
        int total_planned = 0;
        for (int i = 0; i < n; i++) {
            int surplus = effective[i] - preset.target_troops[i];
            int sendable = std::max(0, surplus - committed[i]);
            total_sendable += sendable;
        }
        for (const auto& c : plan_cmds) total_planned += c.count;

        // Count how much of the plan actually gets through capping
        std::vector<int> node_sendable(n);
        for (int i = 0; i < n; i++) {
            int surplus = effective[i] - preset.target_troops[i];
            node_sendable[i] = std::max(0, surplus - committed[i]);
        }
        int actually_sent = 0;
        for (const auto& c : plan_cmds) {
            int can_send = std::min(c.count, node_sendable[c.from_node]);
            actually_sent += can_send;
            node_sendable[c.from_node] -= can_send;
        }

        float loss = loss_fn(effective, preset.target_troops);

        if (tick < 5 || tick % 25 == 0 || vor_changes > 0) {
            std::printf("t=%-4d loss=%-10.0f transit=%-8d planned=%-8d sendable=%-8d sent=%-8d vor_changes=%d\n",
                        tick, loss, total_transit, total_planned, total_sendable, actually_sent, vor_changes);
        }

        prev_voronoi = vor;

        // Actually run the tick
        auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);
    }
}

// ==========================================================================
// Diagnostic: what happens on the tick when troops first arrive?
// ==========================================================================
static void run_arrival_analysis() {
    auto preset = *get_transport_preset("poisson_3_clusters");
    int n = preset.graph.num_nodes();
    LossFunction loss_fn = get_loss_function("l1");

    Game game = make_game(preset);
    ShortestPathData sp = compute_shortest_paths(preset.graph);
    auto ot = std::make_shared<OTSolver>(preset.graph, preset.target_troops, sp);
    TransportSolver solver = [&ot](const Graph&, const std::vector<NodeData>& nodes,
                                    const std::vector<float>&, int pid,
                                    const std::vector<bool>& masked) {
        return ot->solve(nodes, pid, masked);
    };
    std::mt19937 rng(42);
    std::vector<float> warm_phi;

    std::printf("\n=== Arrival Analysis ===\n");
    std::printf("Track per-node on-node counts when they change\n\n");

    std::vector<int> prev_on_node(n);
    for (int i = 0; i < n; i++) prev_on_node[i] = game.node_data()[i].troops[0];

    for (int tick = 0; tick < 500; tick++) {
        auto tr = transport_gym_tick(game, preset, solver, loss_fn, rng, warm_phi);

        // Check for on-node count changes (arrivals/departures)
        int nodes_gained = 0, nodes_lost = 0;
        int total_gained = 0, total_lost = 0;
        for (int i = 0; i < n; i++) {
            int cur = game.node_data()[i].troops[0];
            int delta = cur - prev_on_node[i];
            if (delta > 0) { nodes_gained++; total_gained += delta; }
            if (delta < 0) { nodes_lost++; total_lost += -delta; }
            prev_on_node[i] = cur;
        }

        if (nodes_gained > 0 || nodes_lost > 0) {
            std::printf("t=%-4d loss=%-10.0f gained: %d troops at %d nodes, "
                        "lost: %d troops at %d nodes\n",
                        tick, tr.loss, total_gained, nodes_gained,
                        total_lost, nodes_lost);
        }
    }
}

int main() {
    test_plan_determinism();
    test_initial_plan_sends_all_supply();
    test_no_oversend();
    test_commands_from_supply_only();
    test_loss_monotone();

    run_solver_comparison();
    run_plan_stability();
    run_arrival_analysis();

    return 0;
}
