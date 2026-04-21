#include "gyms/ordering_gym.hpp"
#include "gyms/economy_gym.hpp"  // get_economy_solver
#include "systems/economy/economy_solvers.hpp"
#include "systems/ordering/ordering_solvers.hpp"
#include "engine/game.hpp"

OrderingGymResult run_ordering_gym(
    const std::string& economy_solver_name,
    const std::string& ordering_solver_name,
    uint64_t seed,
    const GameConfig& config) {

    OrderingGymResult result;

    EconomySolver eco_solver = get_economy_solver(economy_solver_name);
    OrderingSolver ord_solver = get_ordering_solver(ordering_solver_name);
    if (!eco_solver || !ord_solver) return result;

    // Generate graph via Game constructor
    Game game(config, {0}, seed);
    const Graph& graph = game.graph();
    int n = graph.num_nodes();

    // Set up: player 0 owns all nodes, enough troops to not constrain economy solver
    std::vector<NodeData> nodes(n);
    for (int i = 0; i < n; i++) {
        nodes[i].owner = 0;
        nodes[i].troops = {config.cost_powerplant + 1};
        nodes[i].state = NodeState::DEFAULT;
    }
    nodes[0].state = NodeState::CAPITAL;

    // 1. Economy solver produces BuildPlan
    BuildPlan plan = eco_solver(graph, nodes, 0, config);
    if (plan.steps.empty()) return result;

    // 2. Ordering solver produces permutation
    result.execution_order = ord_solver(plan, graph, nodes, 0, config);
    result.n_steps = static_cast<int>(plan.steps.size());

    // 3. Simulate: accumulate production, deduct costs
    // Reset nodes to just capital (no builds yet, minimal troops)
    for (int i = 0; i < n; i++) {
        nodes[i].state = NodeState::DEFAULT;
        nodes[i].troops = {0};
    }
    nodes[0].state = NodeState::CAPITAL;

    float rate = compute_production_rate(graph, nodes, 0, config);
    float banked_troops = 0.0f;
    int tick = 0;

    for (int idx : result.execution_order) {
        const auto& step = plan.steps[idx];
        float cost = static_cast<float>(building_cost(step.structure, config));

        // Accumulate production until we can afford the build
        while (banked_troops < cost) {
            banked_troops += rate;
            result.accumulated_production += rate;
            result.production_per_tick.push_back(rate);
            tick++;
        }

        // Build it
        banked_troops -= cost;
        nodes[step.node_idx].state = step.structure;
        rate = compute_production_rate(graph, nodes, 0, config);

        result.step_completed_tick.push_back(tick);
        result.production_after_step.push_back(rate);
    }

    result.total_ticks = tick;
    return result;
}
