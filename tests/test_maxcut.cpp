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

// ── Joint QUBO tests ────────────────────────────────────────

static void test_joint_qubo_dimensions() {
    printf("  test_joint_qubo_dimensions... ");

    auto g = make_k4();
    GameConfig config{};
    std::vector<NodeData> nodes(4);
    for (int i = 0; i < 4; i++) {
        nodes[i].owner = 0;
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {100};
    }
    nodes[0].state = NodeState::CAPITAL;

    auto result = qubo_objective_joint(g, nodes, 0, config);

    // K4 with 1 capital → 3 variables per plan → 6 total
    assert(result.n_vars == 3);
    assert(result.qubo.n == 6);
    assert(static_cast<int>(result.qubo.Q.size()) == 6);
    for (int i = 0; i < 6; i++) {
        assert(static_cast<int>(result.qubo.Q[i].size()) == 6);
    }
    assert(static_cast<int>(result.var_to_node.size()) == 3);

    printf("OK\n");
}

static void test_joint_qubo_block_structure() {
    printf("  test_joint_qubo_block_structure... ");

    auto g = make_k4();
    GameConfig config{};
    std::vector<NodeData> nodes(4);
    for (int i = 0; i < 4; i++) {
        nodes[i].owner = 0;
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {100};
    }
    nodes[0].state = NodeState::CAPITAL;

    float mu_c = 2.0f, mu_e = 3.0f, mu_b = 0.5f, cb = 1.0f;
    auto result = qubo_objective_joint(g, nodes, 0, config, mu_c, mu_e, mu_b, cb);
    int N = result.n_vars;

    // Get reference production Q for comparison
    auto prod = qubo_objective_production(g, nodes, 0, config);

    // Bottom-right block should be mu_e * Q_prod
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float expected = mu_e * prod.qubo.Q[i][j];
            assert(std::abs(result.qubo.Q[N + i][N + j] - expected) < 1e-6f);
        }
    }

    // Off-diagonal blocks: only (i, N+i) and (N+i, i) should be non-zero
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j) {
                assert(std::abs(result.qubo.Q[i][N + j] - mu_b / 2.0f) < 1e-6f);
            } else {
                assert(result.qubo.Q[i][N + j] == 0.0f);
                assert(result.qubo.Q[N + i][j] == 0.0f);
            }
        }
    }

    // Top-left block should have extra cost bias vs Q_prod
    float cost_F = static_cast<float>(config.cost_factory);
    float cost_PP = static_cast<float>(config.cost_powerplant);
    float advantage = (cost_PP - cost_F) / (2.0f * cost_PP);
    for (int i = 0; i < N; i++) {
        float expected_diag = mu_c * (prod.qubo.Q[i][i] + cb * advantage);
        assert(std::abs(result.qubo.Q[i][i] - expected_diag) < 1e-6f);
        // Off-diagonal should match scaled production
        for (int j = i + 1; j < N; j++) {
            float expected = mu_c * prod.qubo.Q[i][j];
            assert(std::abs(result.qubo.Q[i][j] - expected) < 1e-6f);
        }
    }

    printf("OK\n");
}

static void test_joint_qubo_bridge_symmetry() {
    printf("  test_joint_qubo_bridge_symmetry... ");

    auto g = make_path(8);
    GameConfig config{};
    std::vector<NodeData> nodes(8);
    for (int i = 0; i < 8; i++) {
        nodes[i].owner = 0;
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {100};
    }
    nodes[0].state = NodeState::CAPITAL;

    auto result = qubo_objective_joint(g, nodes, 0, config);
    int N2 = result.qubo.n;

    // Full matrix should be symmetric
    for (int i = 0; i < N2; i++) {
        for (int j = 0; j < N2; j++) {
            assert(std::abs(result.qubo.Q[i][j] - result.qubo.Q[j][i]) < 1e-8f);
        }
    }

    printf("OK\n");
}

static void test_joint_qubo_sa_convergence() {
    printf("  test_joint_qubo_sa_convergence... ");

    // Use a real game graph for realistic test
    GameConfig config{};
    GameConfig gen_config = config;
    float area = gen_config.region_width * gen_config.region_height;
    gen_config.poisson_intensity = 50.0f / area;
    Game game(gen_config, {0}, 42);
    Graph graph = game.graph();
    std::vector<NodeData> nodes = game.node_data();
    for (int i = 0; i < graph.num_nodes(); i++) {
        nodes[i].owner = 0;
        if (nodes[i].troops.empty()) nodes[i].troops.resize(1, 0);
        nodes[i].troops[0] = config.cost_powerplant + 1;
    }
    nodes[0].state = NodeState::CAPITAL;

    auto result = qubo_objective_joint(graph, nodes, 0, config,
                                       1.0f, 1.0f, 0.1f, 1.0f);
    assert(result.n_vars > 0);

    // Run SA
    auto solution = qubo_solve_sa(result.qubo, 42, 10000, 5.0f);
    assert(solution.objective > 0.0f);
    assert(static_cast<int>(solution.partition.size()) == result.qubo.n);

    // Extract cheap and expensive plans
    int N = result.n_vars;
    int cheap_factories = 0, exp_factories = 0, agreements = 0;
    for (int i = 0; i < N; i++) {
        if (solution.partition[i] > 0) cheap_factories++;
        if (solution.partition[N + i] > 0) exp_factories++;
        if (solution.partition[i] == solution.partition[N + i]) agreements++;
    }

    // Both plans should have a mix of factories and PPs
    assert(cheap_factories > 0);
    assert(cheap_factories < N);
    assert(exp_factories > 0);
    assert(exp_factories < N);

    // Cheap should have more factories than expensive (cost bias)
    assert(cheap_factories >= exp_factories);

    float agreement_pct = 100.0f * static_cast<float>(agreements) / static_cast<float>(N);
    printf("OK (N=%d, cheap_F=%d, exp_F=%d, agree=%.0f%%)\n",
           N, cheap_factories, exp_factories, agreement_pct);
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
    test_joint_qubo_dimensions();
    test_joint_qubo_block_structure();
    test_joint_qubo_bridge_symmetry();
    test_joint_qubo_sa_convergence();

    printf("All tests passed.\n");
    return 0;
}
