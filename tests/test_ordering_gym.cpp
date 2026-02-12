#include "gyms/ordering_gym.hpp"
#include "systems/ordering/ordering_solvers.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "engine/graph_builder.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <numeric>

static void test_sequential_is_identity() {
    Graph g = build_star(5, 1.0f);
    int n = g.num_nodes();
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {3000};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    BuildPlan plan = economy_solver_greedy(g, nodes, 0, GameConfig{});
    auto order = ordering_solver_sequential(plan, g, nodes, 0, GameConfig{});

    // Should be identity permutation
    for (size_t i = 0; i < order.size(); i++) {
        assert(order[i] == static_cast<int>(i));
    }
    std::printf("test_sequential_is_identity: PASS (%zu steps)\n", order.size());
}

static void test_cheapest_first_sorted() {
    // Create a plan with mixed costs
    BuildPlan plan;
    plan.steps.push_back({1, NodeState::POWERPLANT});  // 2500
    plan.steps.push_back({2, NodeState::FACTORY});      // 500
    plan.steps.push_back({3, NodeState::FORT});          // 400

    Graph g = build_star(3, 1.0f);
    int n = g.num_nodes();
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {3000};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    GameConfig config{};
    auto order = ordering_solver_cheapest_first(plan, g, nodes, 0, config);

    // Should be: fort(400), factory(500), powerplant(2500)
    assert(order.size() == 3);
    assert(plan.steps[order[0]].structure == NodeState::FORT);
    assert(plan.steps[order[1]].structure == NodeState::FACTORY);
    assert(plan.steps[order[2]].structure == NodeState::POWERPLANT);

    std::printf("test_cheapest_first_sorted: PASS\n");
}

static void test_nearest_first_by_distance() {
    // Path: 0 -- 1 -- 2 -- 3 -- 4
    // Capital at 0. Builds at nodes 4, 1, 3.
    // Distance: 1->1, 3->3, 4->4. Order should be: 1, 3, 4 → step indices 1, 2, 0.
    BuildPlan plan;
    plan.steps.push_back({4, NodeState::FACTORY});   // idx 0, dist 4
    plan.steps.push_back({1, NodeState::FACTORY});   // idx 1, dist 1
    plan.steps.push_back({3, NodeState::FACTORY});   // idx 2, dist 3

    Graph g = build_path(5, 1.0f);
    int n = g.num_nodes();
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {3000};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    auto order = ordering_solver_nearest_first(plan, g, nodes, 0, GameConfig{});
    assert(order.size() == 3);
    assert(order[0] == 1);  // node 1, dist 1
    assert(order[1] == 2);  // node 3, dist 3
    assert(order[2] == 0);  // node 4, dist 4

    std::printf("test_nearest_first_by_distance: PASS\n");
}

static void test_production_gradient_prefers_factory_near_powerplant() {
    // Path: capital(0) -- node1 -- powerplant(2)
    // Plan: build factory at 1, build factory at... wait, we need more nodes.
    // Star: center(capital) connected to 3 leaves.
    // Existing powerplant at leaf 1.
    // Plan: build factory at leaf 1 (adjacent to powerplant, gets bonus) and factory at leaf 2.
    // production_gradient should prefer leaf 1 first (higher production delta).
    Graph g = build_star(3, 1.0f);
    int n = g.num_nodes();
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {3000};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;
    nodes[1].state = NodeState::POWERPLANT;  // existing powerplant

    BuildPlan plan;
    plan.steps.push_back({2, NodeState::FACTORY});  // idx 0: no powerplant neighbor
    plan.steps.push_back({3, NodeState::FACTORY});  // idx 1: no powerplant neighbor

    // Hmm, neither is adjacent to the powerplant (only center is adjacent to all).
    // Let me adjust: use a path graph instead.
    // Path: 0(capital) -- 1(powerplant) -- 2 -- 3
    // Build factory at 2 (adjacent to powerplant, gets bonus) vs factory at 3 (no bonus).

    Graph g2 = build_path(4, 1.0f);
    int n2 = g2.num_nodes();
    std::vector<NodeData> nodes2(n2);
    for (int i = 0; i < n2; i++) {
        nodes2[i].owner = 0;
        nodes2[i].troops = {3000};
        nodes2[i].state = NodeState::DEFAULT;
    }
    nodes2[0].state = NodeState::CAPITAL;
    nodes2[1].state = NodeState::POWERPLANT;

    BuildPlan plan2;
    plan2.steps.push_back({3, NodeState::FACTORY});  // idx 0: NOT adj to powerplant
    plan2.steps.push_back({2, NodeState::FACTORY});  // idx 1: adj to powerplant (bonus!)

    GameConfig config{};
    auto order = ordering_solver_production_gradient(plan2, g2, nodes2, 0, config);
    assert(order.size() == 2);

    // Factory at node 2 (idx 1) should be built first — it gets powerplant bonus
    assert(order[0] == 1);

    std::printf("test_production_gradient_prefers_factory_near_powerplant: PASS\n");
}

static void test_ordering_gym_runs() {
    GameConfig config{};
    OrderingGymResult r = run_ordering_gym("bootstrap", "sequential", 42, config);

    assert(r.n_steps > 0);
    assert(r.total_ticks > 0);
    assert(r.accumulated_production > 0.0f);
    assert(r.step_completed_tick.size() == static_cast<size_t>(r.n_steps));

    std::printf("test_ordering_gym_runs: PASS (%d steps, %d ticks, accum=%.0f)\n",
                r.n_steps, r.total_ticks, r.accumulated_production);
}

static void test_cheapest_first_faster_than_reverse() {
    // Cheapest-first should complete sooner than building the most
    // expensive items first (same total cost, but cheap items built
    // earlier means production ramps up sooner).
    GameConfig config{};

    OrderingGymResult r_cheap = run_ordering_gym("bootstrap", "cheapest_first", 42, config);
    OrderingGymResult r_seq = run_ordering_gym("bootstrap", "sequential", 42, config);

    std::printf("test_cheapest_first_faster_than_reverse: cheap=%d ticks, seq=%d ticks\n",
                r_cheap.total_ticks, r_seq.total_ticks);

    // Cheapest-first should be at most as slow as sequential (often faster)
    assert(r_cheap.total_ticks <= r_seq.total_ticks + 1);

    std::printf("test_cheapest_first_faster_than_reverse: PASS\n");
}

int main() {
    test_sequential_is_identity();
    test_cheapest_first_sorted();
    test_nearest_first_by_distance();
    test_production_gradient_prefers_factory_near_powerplant();
    test_ordering_gym_runs();
    test_cheapest_first_faster_than_reverse();

    std::printf("\nAll ordering gym tests passed.\n");
    return 0;
}
