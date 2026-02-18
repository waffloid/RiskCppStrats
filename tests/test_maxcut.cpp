#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/game.hpp"
#include "engine/game_config.hpp"
#include "systems/graph_algo/maxcut_solvers.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "gyms/economy_gym.hpp"

// ── Helpers ──────────────────────────────────────────────────

static Graph make_k4() {
    // Complete graph on 4 nodes (square layout)
    Graph g;
    g.nodes = {{0, 0, 0, {}}, {20, 0, 1, {}}, {20, 20, 2, {}}, {0, 20, 3, {}}};
    // Add all 6 edges
    int eidx = 0;
    auto add_edge = [&](int a, int b) {
        float dx = g.nodes[a].x - g.nodes[b].x;
        float dy = g.nodes[a].y - g.nodes[b].y;
        float len = std::sqrt(dx * dx + dy * dy);
        g.edges.push_back({a, b, eidx, len});
        g.nodes[a].neighbor_indices.push_back(b);
        g.nodes[b].neighbor_indices.push_back(a);
        g.node_pair_to_edge_idx[Graph::pack_pair(a, b)] = eidx;
        eidx++;
    };
    add_edge(0, 1);
    add_edge(0, 2);
    add_edge(0, 3);
    add_edge(1, 2);
    add_edge(1, 3);
    add_edge(2, 3);
    return g;
}

static Graph make_path(int n) {
    Graph g;
    for (int i = 0; i < n; i++) {
        g.nodes.push_back({static_cast<float>(i * 10), 0.0f, i, {}});
    }
    for (int i = 0; i < n - 1; i++) {
        float len = 10.0f;
        g.edges.push_back({i, i + 1, i, len});
        g.nodes[i].neighbor_indices.push_back(i + 1);
        g.nodes[i + 1].neighbor_indices.push_back(i);
        g.node_pair_to_edge_idx[Graph::pack_pair(i, i + 1)] = i;
    }
    return g;
}

// ── Tests ────────────────────────────────────────────────────

static void test_objective_value_computation() {
    printf("  test_objective_value_computation... ");

    auto g = make_k4();
    auto inst = QUBOInstance::from_graph(g);

    // Partition {1, 1, -1, -1}: nodes 0,1 on one side, 2,3 on other.
    // Cut edges: (0,2), (0,3), (1,2), (1,3) = 4 edges.
    // For max-cut Q = adjacency, objective = sum_{(i,j)} Q[i][j]*x_i*x_j
    //   = (0,1):+1 + (0,2):-1 + (0,3):-1 + (1,2):-1 + (1,3):-1 + (2,3):+1
    //   = sum of Q[i][j]*x_i*x_j = 1*1 + 1*(-1) + 1*(-1) + 1*(-1) + 1*(-1) + 1*1
    //   = 1 - 1 - 1 - 1 - 1 + 1 = -2
    // But x^T Q x counts each pair twice (Q is symmetric), so:
    //   x^T Q x = 2 * (1 - 1 - 1 - 1 - 1 + 1) = -4
    std::vector<int> part = {1, 1, -1, -1};
    float val = compute_qubo_objective(inst, part);
    assert(std::abs(val - (-4.0f)) < 1e-5f);

    printf("OK\n");
}

static void test_flip_gains_correct() {
    printf("  test_flip_gains_correct... ");

    auto g = make_path(5);
    auto inst = QUBOInstance::from_graph(g);
    std::vector<int> part = {1, -1, 1, -1, 1};

    auto gains = compute_qubo_flip_gains(inst, part);

    // Verify each flip gain by brute force
    float base_obj = compute_qubo_objective(inst, part);
    for (int i = 0; i < 5; i++) {
        auto flipped = part;
        flipped[i] = -flipped[i];
        float new_obj = compute_qubo_objective(inst, flipped);
        float expected_gain = new_obj - base_obj;
        assert(std::abs(gains[i] - expected_gain) < 1e-4f);
    }

    printf("OK\n");
}

static void test_greedy_valid_partition() {
    printf("  test_greedy_valid_partition... ");

    auto g = make_k4();
    auto inst = QUBOInstance::from_graph(g);
    auto sol = qubo_solve_greedy(inst, 42);

    assert(sol.partition.size() == 4);
    for (int v : sol.partition) {
        assert(v == 1 || v == -1);
    }
    // Verify reported objective matches recomputation
    float recomputed = compute_qubo_objective(inst, sol.partition);
    assert(std::abs(sol.objective - recomputed) < 1e-4f);

    printf("OK\n");
}

static void test_sa_improves_on_random() {
    printf("  test_sa_improves_on_random... ");

    auto g = make_k4();
    auto inst = QUBOInstance::from_graph(g);

    // Random partition
    std::vector<int> random_part = {1, 1, 1, 1};
    float random_obj = compute_qubo_objective(inst, random_part);

    // SA should find something at least as good
    auto sol = qubo_solve_sa(inst, 42, 5000, 5.0f);
    assert(sol.objective >= random_obj);

    printf("OK\n");
}

static void test_gw_valid_partition() {
    printf("  test_gw_valid_partition... ");

    auto g = make_k4();
    auto inst = QUBOInstance::from_graph(g);
    auto sol = qubo_solve_gw(inst, 42, 200, 20);

    assert(sol.partition.size() == 4);
    for (int v : sol.partition) {
        assert(v == 1 || v == -1);
    }
    float recomputed = compute_qubo_objective(inst, sol.partition);
    assert(std::abs(sol.objective - recomputed) < 1e-4f);

    printf("OK\n");
}

static void test_stepper_matches_sa() {
    printf("  test_stepper_matches_sa... ");

    auto g = make_path(10);
    auto inst = QUBOInstance::from_graph(g);

    // Both should reach same quality range (not exactly same due to RNG sequence)
    auto sa_sol = qubo_solve_sa(inst, 42, 2000, 3.0f);
    QUBOStepper stepper(inst, 42, 2000, 3.0f);
    stepper.step(2000);

    // Stepper should achieve something reasonable
    assert(stepper.best_objective() >= sa_sol.objective * 0.8f ||
           stepper.best_objective() >= sa_sol.objective);

    printf("OK\n");
}

static void test_production_objective_structure() {
    printf("  test_production_objective_structure... ");

    // Build a small graph with known structure
    auto g = make_path(4);
    GameConfig config{};
    std::vector<NodeData> nodes(4);
    for (int i = 0; i < 4; i++) {
        nodes[i].owner = 0;
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {100};
    }
    nodes[0].state = NodeState::CAPITAL;

    auto eco_inst = qubo_objective_production(g, nodes, 0, config);

    // Should have 3 variables (nodes 1, 2, 3 — node 0 is capital)
    assert(eco_inst.qubo.n == 3);
    assert(eco_inst.var_to_node.size() == 3);
    assert(eco_inst.var_to_node[0] == 1);
    assert(eco_inst.var_to_node[1] == 2);
    assert(eco_inst.var_to_node[2] == 3);

    // Q should be symmetric
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            assert(std::abs(eco_inst.qubo.Q[i][j] - eco_inst.qubo.Q[j][i]) < 1e-8f);
        }
    }

    printf("OK\n");
}

static void test_all_objectives_produce_valid_Q() {
    printf("  test_all_objectives_produce_valid_Q... ");

    auto g = make_k4();
    GameConfig config{};
    std::vector<NodeData> nodes(4);
    for (int i = 0; i < 4; i++) {
        nodes[i].owner = 0;
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {100};
    }
    nodes[0].state = NodeState::CAPITAL;

    for (const auto& name : list_qubo_objectives()) {
        auto builder = get_qubo_objective(name);
        assert(builder != nullptr);

        auto eco_inst = builder(g, nodes, 0, config);
        int nv = eco_inst.qubo.n;
        assert(nv == 3);  // 4 nodes minus 1 capital
        assert(static_cast<int>(eco_inst.qubo.Q.size()) == nv);

        // Check symmetry
        for (int i = 0; i < nv; i++) {
            assert(static_cast<int>(eco_inst.qubo.Q[i].size()) == nv);
            for (int j = 0; j < nv; j++) {
                assert(std::abs(eco_inst.qubo.Q[i][j] - eco_inst.qubo.Q[j][i]) < 1e-8f);
            }
        }
    }

    printf("OK\n");
}

static void test_qubo_economy_solver() {
    printf("  test_qubo_economy_solver... ");

    // Generate a graph via the economy gym pattern
    GameConfig config{};
    auto state = run_economy_gym("qubo", 42, 50, config);

    // Should produce a valid plan with factories and powerplants
    assert(!state.plan.steps.empty());
    assert(state.production_rate > 0.0f);
    assert(state.n_factories > 0);

    // Compare to MCMC
    auto mcmc_state = run_economy_gym("mcmc", 42, 50, config);

    // QUBO should be in the same ballpark as MCMC
    float ratio = state.production_rate / std::max(mcmc_state.production_rate, 0.001f);
    assert(ratio > 0.5f);  // at least half as good

    printf("OK (qubo=%.1f, mcmc=%.1f, ratio=%.2f)\n",
           state.production_rate, mcmc_state.production_rate, ratio);
}

static void test_solver_registry() {
    printf("  test_solver_registry... ");

    auto solvers = list_qubo_solvers();
    assert(solvers.size() == 3);

    for (const auto& name : solvers) {
        auto solver = get_qubo_solver(name);
        assert(solver != nullptr);
    }

    assert(get_qubo_solver("nonexistent") == nullptr);

    printf("OK\n");
}

static void test_objective_registry() {
    printf("  test_objective_registry... ");

    auto objectives = list_qubo_objectives();
    assert(objectives.size() == 5);

    for (const auto& name : objectives) {
        auto builder = get_qubo_objective(name);
        assert(builder != nullptr);
    }

    assert(get_qubo_objective("nonexistent") == nullptr);

    printf("OK\n");
}

// ── Main ─────────────────────────────────────────────────────

int main() {
    printf("test_maxcut:\n");

    test_objective_value_computation();
    test_flip_gains_correct();
    test_greedy_valid_partition();
    test_sa_improves_on_random();
    test_gw_valid_partition();
    test_stepper_matches_sa();
    test_production_objective_structure();
    test_all_objectives_produce_valid_Q();
    test_solver_registry();
    test_objective_registry();
    test_qubo_economy_solver();

    printf("All tests passed.\n");
    return 0;
}
