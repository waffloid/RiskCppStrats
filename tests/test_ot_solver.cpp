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

// === Test 1: Shortest paths on small graph ===

static void test_shortest_paths_4node_path() {
    Graph g = build_path(4, 10.0f);
    auto sp = compute_shortest_paths(g);

    assert(sp.N == 4);
    assert(sp.dist[0 * 4 + 0] == 0.0f);
    assert(std::abs(sp.dist[0 * 4 + 1] - 10.0f) < 0.01f);
    assert(std::abs(sp.dist[0 * 4 + 2] - 20.0f) < 0.01f);
    assert(std::abs(sp.dist[0 * 4 + 3] - 30.0f) < 0.01f);
    assert(std::abs(sp.dist[1 * 4 + 3] - 20.0f) < 0.01f);

    assert(sp.pred[0 * 4 + 3] == 2);
    assert(sp.pred[0 * 4 + 2] == 1);
    assert(sp.pred[0 * 4 + 1] == 0);

    std::printf("test_shortest_paths_4node_path: PASS\n");
}

// === Test 2: Trivial 2-node transport ===

static void test_trivial_2node() {
    Graph g = build_path(2, 10.0f);
    std::vector<int> target = {5, 5};
    OTSolver solver(g, target);

    auto nodes = make_nodes(2, {10, 0});
    std::vector<bool> mask = {false, false};
    auto cmds = solver.solve(nodes, 0, mask);

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

// === Test 3: Triangle with unequal costs ===

static void test_triangle_routing() {
    // 0-1 len=1, 0-2 len=100, 1-2 len=1
    // All 20 troops at node 0, target = [0, 0, 20]
    // Should route 0→1→2 (cost 2) not 0→2 (cost 100)
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

    std::vector<int> target = {0, 0, 20};
    OTSolver solver(g, target);

    auto nodes = make_nodes(3, {20, 0, 0});
    std::vector<bool> mask = {false, false, false};
    auto cmds = solver.solve(nodes, 0, mask);

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

// === Test 4: No movement needed ===

static void test_no_movement_balanced() {
    Graph g = build_path(3, 10.0f);
    std::vector<int> target = {100, 100, 100};
    OTSolver solver(g, target);

    auto nodes = make_nodes(3, {100, 100, 100});
    std::vector<bool> mask = {false, false, false};
    auto cmds = solver.solve(nodes, 0, mask);

    assert(cmds.empty());
    std::printf("test_no_movement_balanced: PASS\n");
}

// === Test 5: Masked source node ===

static void test_masked_source() {
    Graph g = build_path(3, 10.0f);
    std::vector<int> target = {0, 50, 50};
    OTSolver solver(g, target);

    auto nodes = make_nodes(3, {100, 0, 0});
    std::vector<bool> mask = {true, false, false};
    auto cmds = solver.solve(nodes, 0, mask);

    for (const auto& c : cmds) {
        assert(c.from_node != 0);
    }
    std::printf("test_masked_source: PASS\n");
}

// === Test 6: Repeated solves produce consistent results ===

static void test_repeated_solves() {
    Graph g = build_star(5);
    int n = g.num_nodes();
    std::vector<int> target(n, 1000 / n);
    int rem = 1000 - (1000 / n) * n;
    for (int i = 0; i < rem; i++) target[i]++;

    OTSolver solver(g, target);

    std::vector<int> init(n, 0);
    init[0] = 1000;
    auto nodes = make_nodes(n, init);
    std::vector<bool> mask(n, false);

    auto cmds1 = solver.solve(nodes, 0, mask);
    auto cmds2 = solver.solve(nodes, 0, mask);

    assert(!cmds1.empty());
    assert(!cmds2.empty());

    int sent1 = 0, sent2 = 0;
    for (const auto& c : cmds1) sent1 += c.count;
    for (const auto& c : cmds2) sent2 += c.count;
    assert(sent1 == sent2);

    std::printf("test_repeated_solves: PASS (sent=%d both times)\n", sent1);
}

// === Test 7: Flow conservation ===

static void test_flow_conservation() {
    Graph g = build_star(8);
    int n = g.num_nodes();
    int total = 900;
    std::vector<int> target(n, total / n);

    std::vector<int> initial(n, 0);
    initial[0] = total;

    OTSolver solver(g, target);
    auto nodes = make_nodes(n, initial);
    std::vector<bool> mask(n, false);
    auto cmds = solver.solve(nodes, 0, mask);

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

// === Test 8: Multi-hop decomposition (4-node chain) ===

static void test_multihop_chain() {
    // 0 -- 1 -- 2 -- 3, all 30 troops at 0, target at 3
    Graph g = build_path(4, 10.0f);
    std::vector<int> target = {0, 0, 0, 30};
    OTSolver solver(g, target);

    auto nodes = make_nodes(4, {30, 0, 0, 0});
    std::vector<bool> mask = {false, false, false, false};
    auto cmds = solver.solve(nodes, 0, mask);

    // Node 0 should send to node 1 only (first hop)
    bool has_0_to_1 = false;
    for (const auto& c : cmds) {
        if (c.from_node == 0 && c.to_node == 1) has_0_to_1 = true;
        if (c.from_node == 0) assert(c.to_node == 1);
    }
    assert(has_0_to_1);

    std::printf("test_multihop_chain: PASS\n");
}

int main() {
    test_shortest_paths_4node_path();
    test_trivial_2node();
    test_triangle_routing();
    test_no_movement_balanced();
    test_masked_source();
    test_repeated_solves();
    test_flow_conservation();
    test_multihop_chain();

    std::printf("\nAll OT solver tests passed.\n");
    return 0;
}
