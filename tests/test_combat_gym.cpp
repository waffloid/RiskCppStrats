#include "gyms/combat_gym.hpp"
#include "gyms/combat_benchmarks.hpp"
#include "systems/combat/combat_solvers.hpp"

#include <cassert>
#include <cstdio>

static void test_benchmarks_are_valid() {
    auto names = list_combat_benchmarks();
    assert(!names.empty());

    for (const auto& name : names) {
        auto bm = get_combat_benchmark(name);
        assert(bm.has_value());
        assert(!bm->name.empty());
        assert(bm->capitals.size() >= 2);
        assert(bm->graph.num_nodes() >= 2);
        assert(bm->max_ticks > 0);
        std::printf("  benchmark '%s': %d nodes, %d edges, %zu overrides\n",
                    name.c_str(), bm->graph.num_nodes(), bm->graph.num_edges(),
                    bm->overrides.size());
    }

    std::printf("test_benchmarks_are_valid: PASS\n");
}

static void test_unknown_benchmark_returns_empty() {
    auto bm = get_combat_benchmark("nonexistent_benchmark");
    assert(!bm.has_value());
    std::printf("test_unknown_benchmark_returns_empty: PASS\n");
}

static void test_grid_topology() {
    auto bm = get_combat_benchmark("grid_5x5");
    assert(bm.has_value());
    assert(bm->graph.num_nodes() == 25);
    // 5x5 grid: 4*5 horizontal + 5*4 vertical = 40 edges
    assert(bm->graph.num_edges() == 40);

    // Corner nodes have degree 2, edge nodes 3, interior nodes 4
    assert(bm->graph.nodes[0].neighbor_indices.size() == 2);   // top-left
    assert(bm->graph.nodes[24].neighbor_indices.size() == 2);  // bottom-right
    assert(bm->graph.nodes[2].neighbor_indices.size() == 3);   // top edge
    assert(bm->graph.nodes[12].neighbor_indices.size() == 4);  // center

    std::printf("test_grid_topology: PASS\n");
}

static void test_combat_gym_runs_corridor() {
    auto bm = get_combat_benchmark("corridor");
    assert(bm.has_value());

    // Random vs random — fast, tests the mechanics
    CombatGymResult r = run_combat_gym(*bm, "random", "random");

    assert(r.ticks_elapsed > 0);
    assert(r.nodes_p0 + r.nodes_p1 <= bm->graph.num_nodes());
    assert(r.troops_p0 > 0 || r.troops_p1 > 0);

    std::printf("test_combat_gym_runs_corridor: %d ticks, winner=%d, "
                "nodes=(%d,%d), troops=(%d,%d), deaths=(%d,%d)\n",
                r.ticks_elapsed, r.winner,
                r.nodes_p0, r.nodes_p1,
                r.troops_p0, r.troops_p1,
                r.deaths_p0, r.deaths_p1);
    std::printf("test_combat_gym_runs_corridor: PASS\n");
}

static void test_combat_gym_spartan() {
    auto bm = get_combat_benchmark("corridor");
    assert(bm.has_value());

    CombatGymResult r = run_combat_gym(*bm, "random", "random", 3.0f);

    assert(r.spartan_multiplier == 3.0f);
    assert(r.ticks_elapsed > 0);

    std::printf("test_combat_gym_spartan: %d ticks, winner=%d, deaths=(%d,%d)\n",
                r.ticks_elapsed, r.winner, r.deaths_p0, r.deaths_p1);
    std::printf("test_combat_gym_spartan: PASS\n");
}

static void test_combat_gym_ai_vs_random() {
    auto bm = get_combat_benchmark("bipartite_3_5");
    assert(bm.has_value());

    CombatGymResult r = run_combat_gym(*bm, "v2_knapsack", "random");

    std::printf("test_combat_gym_ai_vs_random: %d ticks, winner=%d, "
                "nodes=(%d,%d), troops=(%d,%d)\n",
                r.ticks_elapsed, r.winner,
                r.nodes_p0, r.nodes_p1,
                r.troops_p0, r.troops_p1);
    std::printf("test_combat_gym_ai_vs_random: PASS\n");
}

static void test_solver_list_matches_models() {
    auto solvers = list_combat_solvers();
    assert(!solvers.empty());

    // Every listed solver should be retrievable
    for (const auto& name : solvers) {
        assert(get_combat_solver(name) != nullptr);
    }

    std::printf("test_solver_list_matches_models: PASS (%zu solvers)\n", solvers.size());
}

int main() {
    test_benchmarks_are_valid();
    test_unknown_benchmark_returns_empty();
    test_grid_topology();
    test_combat_gym_runs_corridor();
    test_combat_gym_spartan();
    test_combat_gym_ai_vs_random();
    test_solver_list_matches_models();

    std::printf("\nAll combat gym tests passed.\n");
    return 0;
}
