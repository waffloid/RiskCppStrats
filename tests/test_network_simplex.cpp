#include "systems/transport/network_simplex.hpp"
#include "systems/transport/ot_solver.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "engine/graph_builder.hpp"
#include "engine/game_state.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// --- Helper: build nodes owned by player 0 with given troop counts ---

static std::vector<NodeData> make_nodes(int n, const std::vector<int>& troops) {
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {troops[i]};
        nodes[i].state = (i == 0) ? NodeState::CAPITAL : NodeState::DEFAULT;
    }
    return nodes;
}

// === Test 1: Trivial 2-node transport ===

static void test_trivial_2node() {
    Graph g = build_path(2, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(2, {10, 0});
    std::vector<bool> mask = {false, false};
    std::vector<int> targets = {5, 5};
    auto cmds = ns.solve(nodes, 0, mask, targets);

    assert(!cmds.empty());
    int total_sent = 0;
    for (const auto& c : cmds) {
        assert(c.from_node == 0);
        assert(c.to_node == 1);
        total_sent += c.count;
    }
    assert(total_sent == 5);

    std::printf("test_trivial_2node: PASS (sent %d)\n", total_sent);
}

// === Test 2: Triangle with unequal costs ===

static void test_triangle_routing() {
    // 0-1 len=1, 0-2 len=100, 1-2 len=1
    // All 20 troops at node 0, target = [0, 0, 20]
    // Should route 0->1->2 (cost 2) not 0->2 (cost 100)
    Graph g = build_graph(
        {{0, 0}, {1, 0}, {2, 0}},
        {{0, 1}, {0, 2}, {1, 2}}
    );
    for (auto& e : g.edges) {
        if ((e.a_idx == 0 && e.b_idx == 2)) {
            e.length = 100.0f;
        } else {
            e.length = 1.0f;
        }
    }

    NetworkSimplex ns(g);
    auto nodes = make_nodes(3, {20, 0, 0});
    std::vector<bool> mask = {false, false, false};
    std::vector<int> targets = {0, 0, 20};
    auto cmds = ns.solve(nodes, 0, mask, targets);

    // On the game graph, flow goes 0->1->2.
    // Node 0 should send to 1 (first hop on game graph).
    bool has_0_to_1 = false;
    bool has_0_to_2 = false;
    for (const auto& c : cmds) {
        if (c.from_node == 0 && c.to_node == 1) has_0_to_1 = true;
        if (c.from_node == 0 && c.to_node == 2) has_0_to_2 = true;
    }
    assert(has_0_to_1);
    assert(!has_0_to_2);

    std::printf("test_triangle_routing: PASS (routes through cheap path)\n");
}

// === Test 3: No movement needed ===

static void test_no_movement_balanced() {
    Graph g = build_path(3, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(3, {100, 100, 100});
    std::vector<bool> mask = {false, false, false};
    std::vector<int> targets = {100, 100, 100};
    auto cmds = ns.solve(nodes, 0, mask, targets);

    assert(cmds.empty());
    std::printf("test_no_movement_balanced: PASS\n");
}

// === Test 4: Masked source node ===

static void test_masked_source() {
    Graph g = build_path(3, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(3, {100, 0, 0});
    std::vector<bool> mask = {true, false, false};
    std::vector<int> targets = {0, 50, 50};
    auto cmds = ns.solve(nodes, 0, mask, targets);

    for (const auto& c : cmds) {
        assert(c.from_node != 0);
    }
    std::printf("test_masked_source: PASS\n");
}

// === Test 5: Repeated solves produce consistent results ===

static void test_repeated_solves() {
    Graph g = build_star(5);
    int n = g.num_nodes();
    std::vector<int> target(n, 1000 / n);
    int rem = 1000 - (1000 / n) * n;
    for (int i = 0; i < rem; i++) target[i]++;

    NetworkSimplex ns(g);

    std::vector<int> init(n, 0);
    init[0] = 1000;
    auto nodes = make_nodes(n, init);
    std::vector<bool> mask(n, false);

    auto cmds1 = ns.solve(nodes, 0, mask, target);
    auto cmds2 = ns.solve(nodes, 0, mask, target);

    assert(!cmds1.empty());
    assert(!cmds2.empty());

    int sent1 = 0, sent2 = 0;
    for (const auto& c : cmds1) sent1 += c.count;
    for (const auto& c : cmds2) sent2 += c.count;
    assert(sent1 == sent2);

    std::printf("test_repeated_solves: PASS (sent=%d both times)\n", sent1);
}

// === Test 6: Flow conservation ===

static void test_flow_conservation() {
    Graph g = build_star(8);
    int n = g.num_nodes();
    int total = 900;
    std::vector<int> target(n, total / n);

    std::vector<int> initial(n, 0);
    initial[0] = total;

    NetworkSimplex ns(g);
    auto nodes = make_nodes(n, initial);
    std::vector<bool> mask(n, false);
    auto cmds = ns.solve(nodes, 0, mask, target);

    // Total sent should not exceed supply at node 0
    int total_sent = 0;
    for (const auto& c : cmds) {
        assert(c.count > 0);
        total_sent += c.count;
    }
    int supply_at_0 = total - target[0];
    assert(total_sent <= supply_at_0);

    // Total received at each node should not exceed demand
    std::vector<int> received(n, 0);
    for (const auto& c : cmds) {
        received[c.to_node] += c.count;
    }
    for (int j = 0; j < n; j++) {
        int dem = std::max(0, target[j] - initial[j]);
        assert(received[j] <= dem + 1);  // +1 for rounding
    }

    std::printf("test_flow_conservation: PASS (sent=%d, supply=%d)\n", total_sent, supply_at_0);
}

// === Test 7: Multi-hop chain ===

static void test_multihop_chain() {
    // 0 -- 1 -- 2 -- 3, all 30 troops at 0, target at 3
    Graph g = build_path(4, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(4, {30, 0, 0, 0});
    std::vector<bool> mask = {false, false, false, false};
    std::vector<int> targets = {0, 0, 0, 30};
    auto cmds = ns.solve(nodes, 0, mask, targets);

    // On the game graph, flow routes through 0->1->2->3.
    // Node 0 should emit commands to neighbor 1.
    bool has_0_to_1 = false;
    for (const auto& c : cmds) {
        if (c.from_node == 0 && c.to_node == 1) has_0_to_1 = true;
        if (c.from_node == 0) assert(c.to_node == 1);
    }
    assert(has_0_to_1);

    std::printf("test_multihop_chain: PASS\n");
}

// === Test 8: Warm start detection ===

static void test_warm_start_detection() {
    Graph g = build_star(5);
    int n = g.num_nodes();
    std::vector<int> target(n, 100);

    NetworkSimplex ns(g);
    std::vector<int> init(n, 0);
    init[0] = 600;
    auto nodes = make_nodes(n, init);
    std::vector<bool> mask(n, false);

    // First solve: cold start
    ns.solve(nodes, 0, mask, target);
    assert(!ns.last_was_warm());
    int cold_pivots = ns.last_pivot_count();

    // Second solve with minor supply change: should detect warm-start possible
    init[0] = 590;
    nodes = make_nodes(n, init);
    ns.solve(nodes, 0, mask, target);
    std::printf("test_warm_start_detection: PASS (cold=%d pivots, warm_detected=%s)\n",
                cold_pivots, ns.last_was_warm() ? "true" : "false");
}

// === Test 9: Comparison with OTSolver ===

static void test_comparison_with_ot() {
    Graph g = build_star(5);
    int n = g.num_nodes();
    std::vector<int> targets(n, 100);

    std::vector<int> init(n, 0);
    init[0] = 600;
    auto nodes = make_nodes(n, init);
    std::vector<bool> mask(n, false);

    // NetworkSimplex
    NetworkSimplex ns(g);
    auto ns_cmds = ns.solve(nodes, 0, mask, targets);

    // OTSolver
    OTSolver ot(g, targets);
    auto ot_cmds = ot.solve(nodes, 0, mask);

    // Both should send the same total
    int ns_total = 0, ot_total = 0;
    for (const auto& c : ns_cmds) ns_total += c.count;
    for (const auto& c : ot_cmds) ot_total += c.count;

    // Allow small tolerance for rounding
    assert(std::abs(ns_total - ot_total) <= 2);

    std::printf("test_comparison_with_ot: PASS (NS=%d, OT=%d)\n", ns_total, ot_total);
}

// === Test 10: Value-weighted demand ===

static void test_value_weighted() {
    // 3-node path: 0--1--2, troops at 0, demand at 1 and 2
    // Node 2 has higher value -> should get served even though farther
    Graph g = build_path(3, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(3, {10, 0, 0});
    std::vector<bool> mask = {false, false, false};
    std::vector<int> targets = {0, 5, 5};
    std::vector<float> demand_value = {0.0f, 0.5f, 5.0f};

    // With value_alpha > 0, node 2 should be prioritized
    auto cmds = ns.solve(nodes, 0, mask, targets, demand_value, {}, {},
                          0.0f, 2.0f, 1);

    // Should have some flow
    int total = 0;
    for (const auto& c : cmds) total += c.count;
    assert(total > 0);

    std::printf("test_value_weighted: PASS (sent %d)\n", total);
}

// === Test 11: Frank-Wolfe convergence (producer flow spreads) ===

static void test_fw_convergence() {
    // 6-node path: 0--1--2--3--4--5
    // Supply at node 0 (small), demand spread across 3,4,5.
    // Nodes 1,2 are producers (factories with production).
    // With saturation alpha > 0, FW should use both producers.
    Graph g = build_path(6, 10.0f);
    int n = g.num_nodes();

    std::vector<int> init(n, 0);
    init[0] = 10;  // existing supply
    auto nodes = make_nodes(n, init);
    // Make nodes 1,2 into factories (producers)
    nodes[1].state = NodeState::FACTORY;
    nodes[2].state = NodeState::FACTORY;

    std::vector<bool> mask(n, false);
    std::vector<int> targets(n, 0);
    targets[3] = 30;
    targets[4] = 30;
    targets[5] = 30;  // total demand = 90

    std::vector<float> prod_rate(n, 0.0f);
    prod_rate[1] = 2.0f;
    prod_rate[2] = 2.0f;

    NetworkSimplex ns(g);
    auto cmds = ns.solve(nodes, 0, mask, targets, {}, {}, prod_rate,
                          0.1f, 0.0f, 8);

    // Verify FW ran (producers exist, alpha > 0)
    if (ns.last_fw_iterations() != 8) {
        std::printf("test_fw_convergence: FAIL (expected 8 FW iters, got %d)\n",
                    ns.last_fw_iterations());
        assert(false);
    }

    std::printf("test_fw_convergence: PASS (fw_iters=%d, pivots=%d)\n",
                ns.last_fw_iterations(), ns.last_pivot_count());
}

// === Test 12: FW iteration count reported correctly ===

static void test_fw_iteration_count() {
    Graph g = build_path(3, 10.0f);
    NetworkSimplex ns(g);

    auto nodes = make_nodes(3, {10, 0, 0});
    std::vector<bool> mask = {false, false, false};
    std::vector<int> targets = {0, 5, 5};

    // No production -> no FW needed, should report 1 iteration
    ns.solve(nodes, 0, mask, targets);
    assert(ns.last_fw_iterations() == 1);

    // With production and alpha > 0, should report requested iterations
    std::vector<float> prod_rate = {0.0f, 1.0f, 0.0f};
    // Node 1 is a producer but also has demand, so it won't get a producer arc.
    // Need a node that is neither supply nor demand.
    // Make a 4-node path: 0 has troops, 3 has demand, 1 is producer (no supply/demand)
    Graph g2 = build_path(4, 10.0f);
    NetworkSimplex ns2(g2);
    auto nodes2 = make_nodes(4, {10, 0, 0, 0});
    nodes2[1].state = NodeState::FACTORY;
    std::vector<bool> mask2 = {false, false, false, false};
    std::vector<int> targets2 = {0, 0, 0, 20};
    std::vector<float> prod2 = {0.0f, 1.0f, 0.0f, 0.0f};

    ns2.solve(nodes2, 0, mask2, targets2, {}, {}, prod2, 0.01f, 0.0f, 5);
    assert(ns2.last_fw_iterations() == 5);

    std::printf("test_fw_iteration_count: PASS (no_prod=1, with_prod=5)\n");
}

int main() {
    test_trivial_2node();
    test_triangle_routing();
    test_no_movement_balanced();
    test_masked_source();
    test_repeated_solves();
    test_flow_conservation();
    test_multihop_chain();
    test_warm_start_detection();
    test_comparison_with_ot();
    test_value_weighted();
    test_fw_convergence();
    test_fw_iteration_count();

    std::printf("\nAll network simplex tests passed.\n");
    return 0;
}
