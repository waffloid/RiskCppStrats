#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/graph_builder.hpp"
#include "engine/game_state.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>

// --- Loss function unit tests ---

static void test_loss_l1() {
    std::vector<int> current = {100, 0, 50};
    std::vector<int> target  = {50, 50, 50};
    float l = loss_l1(current, target);
    assert(l == 100.0f);  // |50| + |50| + |0| = 100
    std::printf("test_loss_l1: PASS (loss=%.1f)\n", l);
}

static void test_loss_l2() {
    std::vector<int> current = {100, 0};
    std::vector<int> target  = {50, 50};
    float l = loss_l2(current, target);
    float expected = std::sqrt(50.0f * 50.0f + 50.0f * 50.0f);
    assert(std::abs(l - expected) < 0.01f);
    std::printf("test_loss_l2: PASS (loss=%.1f)\n", l);
}

static void test_loss_max_deficit() {
    std::vector<int> current = {200, 0, 100};
    std::vector<int> target  = {50, 50, 50};
    float l = loss_max_deficit(current, target);
    assert(l == 50.0f);  // only node 1 has deficit: 50 - 0 = 50
    std::printf("test_loss_max_deficit: PASS (loss=%.1f)\n", l);
}

static void test_loss_perfect_match() {
    std::vector<int> dist = {50, 50, 50};
    assert(loss_l1(dist, dist) == 0.0f);
    assert(loss_l2(dist, dist) == 0.0f);
    assert(loss_max_deficit(dist, dist) == 0.0f);
    std::printf("test_loss_perfect_match: PASS\n");
}

// --- Solver unit tests ---

static void test_greedy_solver_moves_from_surplus() {
    // Star graph: center has 1000 troops, leaves have 0.
    // Gradient = target - current. Target = uniform 100 each.
    // Center gradient = 100 - 1000 = -900 (surplus), leaves = 100 - 0 = 100 (deficit).
    // Solver should send troops from center to leaves.
    Graph g = build_star(5);
    int n = g.num_nodes();

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {0};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].troops[0] = 1000;
    nodes[0].state = NodeState::CAPITAL;

    std::vector<float> gradient(n);
    int target_per_node = 1000 / n;
    for (int i = 0; i < n; i++) {
        gradient[i] = static_cast<float>(target_per_node - nodes[i].troops[0]);
    }

    std::vector<bool> no_mask(n, false);
    auto cmds = transport_solver_greedy(g, nodes, gradient, 0, no_mask, 0.1f, 1);

    assert(!cmds.empty());

    // All commands should originate from center (node 0)
    int total_sent = 0;
    for (const auto& c : cmds) {
        assert(c.from_node == 0);
        total_sent += c.count;
    }
    assert(total_sent > 0);

    std::printf("test_greedy_solver_moves_from_surplus: PASS (sent %d troops)\n", total_sent);
}

static void test_greedy_solver_no_flow_when_uniform() {
    // If gradient is uniform (all same value), no positive diffs → no flow.
    Graph g = build_path(5);
    int n = g.num_nodes();

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {100};
        nodes[i].state = NodeState::DEFAULT;
    }

    std::vector<float> gradient(n, 0.0f);  // all zero = at target
    std::vector<bool> no_mask(n, false);
    auto cmds = transport_solver_greedy(g, nodes, gradient, 0, no_mask, 0.1f, 1);

    assert(cmds.empty());
    std::printf("test_greedy_solver_no_flow_when_uniform: PASS\n");
}

static void test_greedy_solver_respects_mask() {
    // Mask node 0 → should not produce commands from node 0.
    Graph g = build_path(3);
    int n = g.num_nodes();

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {100};
        nodes[i].state = NodeState::DEFAULT;
    }

    // Strong gradient pulling from node 0 to node 1
    std::vector<float> gradient = {-100.0f, 100.0f, 0.0f};
    std::vector<bool> mask = {true, false, false};

    auto cmds = transport_solver_greedy(g, nodes, gradient, 0, mask, 0.1f, 1);

    for (const auto& c : cmds) {
        assert(c.from_node != 0);  // node 0 is masked
    }
    std::printf("test_greedy_solver_respects_mask: PASS\n");
}

// --- Gym tests ---

static void test_presets_are_valid() {
    auto names = list_transport_presets();
    assert(!names.empty());

    for (const auto& name : names) {
        auto p = get_transport_preset(name);
        assert(p.has_value());
        assert(!p->name.empty());
        assert(p->graph.num_nodes() > 0);
        assert(p->initial_troops.size() == static_cast<size_t>(p->graph.num_nodes()));
        assert(p->target_troops.size() == static_cast<size_t>(p->graph.num_nodes()));

        // Total troops should be conserved between initial and target
        int init_total = 0, target_total = 0;
        for (int t : p->initial_troops) init_total += t;
        for (int t : p->target_troops) target_total += t;
        assert(init_total == target_total);

        std::printf("  preset '%s': %d nodes, %d total troops\n",
                    name.c_str(), p->graph.num_nodes(), init_total);
    }
    std::printf("test_presets_are_valid: PASS\n");
}

static void test_transport_gym_loss_decreases() {
    auto preset = get_transport_preset("star_center");
    assert(preset.has_value());

    TransportGymResult r = run_transport_gym(*preset, "greedy", "l1", 200);

    assert(!r.ticks.empty());
    assert(r.initial_loss > 0.0f);
    // Loss should decrease over time (maybe not monotonically due to transit)
    assert(r.final_loss < r.initial_loss);

    std::printf("test_transport_gym_loss_decreases: PASS (%.1f -> %.1f)\n",
                r.initial_loss, r.final_loss);
}

static void test_transport_gym_path_convergence() {
    auto preset = get_transport_preset("path_end");
    assert(preset.has_value());

    TransportGymResult r = run_transport_gym(*preset, "greedy", "l2", 500);

    assert(r.final_loss < r.initial_loss);

    std::printf("test_transport_gym_path_convergence: PASS (%.1f -> %.1f, %d ticks)\n",
                r.initial_loss, r.final_loss, r.total_ticks);
}

static void test_poisson_edge_ring_converges() {
    auto preset = get_transport_preset("poisson_edge_ring");
    assert(preset.has_value());

    TransportGymResult r = run_transport_gym(*preset, "greedy", "l1", 5000);

    assert(!r.ticks.empty());
    assert(r.initial_loss > 0.0f);
    assert(r.final_loss < r.initial_loss);

    std::printf("test_poisson_edge_ring_converges: PASS (%.1f -> %.1f, %d nodes)\n",
                r.initial_loss, r.final_loss, preset->graph.num_nodes());
}

int main() {
    test_loss_l1();
    test_loss_l2();
    test_loss_max_deficit();
    test_loss_perfect_match();

    test_greedy_solver_moves_from_surplus();
    test_greedy_solver_no_flow_when_uniform();
    test_greedy_solver_respects_mask();

    test_presets_are_valid();
    test_transport_gym_loss_decreases();
    test_transport_gym_path_convergence();
    test_poisson_edge_ring_converges();

    std::printf("\nAll transport gym tests passed.\n");
    return 0;
}
