#include "systems/economy/economy_solvers.hpp"
#include "engine/graph_builder.hpp"
#include "engine/game.hpp"

#include <cassert>
#include <cstdio>

static void test_greedy_solver_builds_on_star() {
    // Star graph: center + 5 leaves.  Player owns all.
    // Center is capital.  Greedy should build factories on leaves.
    Graph graph = build_star(5, 1.0f);
    int n = graph.num_nodes();
    GameConfig config{};

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops.resize(2, 0);
        nodes[i].troops[0] = config.cost_powerplant + 1;
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    BuildPlan plan = economy_solver_greedy(graph, nodes, 0, config);

    // Should produce at least some builds.
    assert(!plan.steps.empty());

    // All builds should be for player-owned nodes.
    for (const auto& cmd : plan.steps) {
        assert(cmd.node_idx >= 0 && cmd.node_idx < n);
        assert(nodes[cmd.node_idx].owner == 0);
    }

    std::printf("test_greedy_solver_builds_on_star: PASS\n");
}

static void test_bootstrap_produces_plan() {
    // Create a game with a Poisson graph, then test bootstrap.
    GameConfig config{};
    std::vector<int> capitals = {0};
    Game game(config, capitals, 42);

    // Give player ownership of nearby nodes.
    std::vector<NodeData> nodes = game.node_data();
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        nodes[i].owner = 0;
        if (nodes[i].troops.empty()) nodes[i].troops.resize(2, 0);
    }
    nodes[0].state = NodeState::CAPITAL;

    BuildPlan plan = economy_solver_bootstrap(game.graph(), nodes, 0, config, 4);

    // Should produce exactly 5 steps: 4 factories + 1 powerplant.
    assert(plan.steps.size() == 5);

    int factories = 0, powerplants = 0;
    for (const auto& cmd : plan.steps) {
        if (cmd.structure == NodeState::FACTORY) factories++;
        if (cmd.structure == NodeState::POWERPLANT) powerplants++;
    }
    assert(factories == 4);
    assert(powerplants == 1);

    std::printf("test_bootstrap_produces_plan: PASS\n");
}

static void test_production_rate_calculation() {
    // Path graph: A - B - C.
    // A = capital (player 0), B = factory (player 0), C = powerplant (player 0).
    // B should be powered by C.
    Graph graph = build_path(3, 1.0f);
    GameConfig config{};

    std::vector<NodeData> nodes(3);
    for (int i = 0; i < 3; i++) {
        nodes[i].owner = 0;
        nodes[i].troops.resize(2, 0);
    }
    nodes[0].state = NodeState::CAPITAL;
    nodes[1].state = NodeState::FACTORY;
    nodes[2].state = NodeState::POWERPLANT;

    float rate = compute_production_rate(graph, nodes, 0, config);
    // Capital produces capital_troops_per_tick (2).
    // Factory produces factory_troops_per_tick (1).
    // Factory is adjacent to powerplant: +powerplant_bonus (2).
    // Capital is NOT adjacent to powerplant (A-B-C, A not adj C).
    float expected = static_cast<float>(config.capital_troops_per_tick
                                        + config.factory_troops_per_tick
                                        + config.powerplant_bonus);
    assert(rate == expected);

    std::printf("test_production_rate_calculation: PASS (rate=%.1f)\n", rate);
}

static void test_mcmc_produces_plan() {
    // MCMC on a star graph should produce a non-empty plan.
    Graph graph = build_star(5, 1.0f);
    int n = graph.num_nodes();
    GameConfig config{};

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops.resize(2, 0);
        nodes[i].troops[0] = config.cost_powerplant + 1;
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    BuildPlan plan = economy_solver_mcmc(graph, nodes, 0, config);
    assert(!plan.steps.empty());

    // Branch-and-bound is still a stub
    BuildPlan bb = economy_solver_branch_bound(graph, nodes, 0, config);
    assert(bb.steps.empty());

    std::printf("test_mcmc_produces_plan: PASS\n");
}

static void test_theoretical_max_is_upper_bound() {
    // Star graph: center=capital, 5 leaves, all owned, no buildings yet.
    // theoretical_max = (capital_rate + bonus) + 5 * (factory_rate + bonus)
    //                 = (2 + 2) + 5 * (1 + 2) = 4 + 15 = 19
    Graph graph = build_star(5, 1.0f);
    int n = graph.num_nodes();
    GameConfig config{};

    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops.resize(2, 0);
        nodes[i].troops[0] = config.cost_powerplant + 1;
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    float theo_max = compute_theoretical_max_production(graph, nodes, 0, config);
    float expected = static_cast<float>(config.capital_troops_per_tick + config.powerplant_bonus)
                   + 5.0f * static_cast<float>(config.factory_troops_per_tick + config.powerplant_bonus);
    assert(theo_max == expected);  // 19

    // Actual production with just the capital should be much less
    float actual = compute_production_rate(graph, nodes, 0, config);
    assert(actual == static_cast<float>(config.capital_troops_per_tick));  // 2
    float efficiency = actual / theo_max;
    assert(efficiency < 0.15f);  // ~10.5%

    // After MCMC, actual should be <= theoretical_max
    BuildPlan plan = economy_solver_mcmc(graph, nodes, 0, config);
    for (const auto& step : plan.steps) {
        nodes[step.node_idx].state = step.structure;
    }
    float after_mcmc = compute_production_rate(graph, nodes, 0, config);
    assert(after_mcmc <= theo_max);
    assert(after_mcmc > actual);  // MCMC should improve over bare capital

    std::printf("test_theoretical_max_is_upper_bound: PASS (max=%.0f, bare=%.0f, mcmc=%.0f, eff=%.1f%%)\n",
                theo_max, actual, after_mcmc, (after_mcmc / theo_max) * 100.0f);
}

int main() {
    test_greedy_solver_builds_on_star();
    test_bootstrap_produces_plan();
    test_production_rate_calculation();
    test_mcmc_produces_plan();
    test_theoretical_max_is_upper_bound();
    std::printf("\nAll economy solver tests passed.\n");
    return 0;
}
