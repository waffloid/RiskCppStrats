#include "gyms/economy_gym.hpp"
#include "engine/game.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"

EconomySolver get_economy_solver(const std::string& name) {
    if (name == "greedy") return economy_solver_greedy;
    if (name == "bootstrap") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_bootstrap(g, n, pid, c);
        };
    }
    if (name == "mcmc") return economy_solver_mcmc;
    if (name == "branch_bound") return economy_solver_branch_bound;
    if (name == "qubo") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "production");
        };
    }
    if (name == "qubo_adjacency") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "adjacency");
        };
    }
    if (name == "qubo_distance") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "distance");
        };
    }
    if (name == "qubo_degree") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "degree");
        };
    }
    if (name == "qubo_composite") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "composite");
        };
    }
    if (name == "qubo_factory_biased") {
        return [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "factory_biased");
        };
    }
    return nullptr;
}

EconomyGymState run_economy_gym(
    const std::string& solver_name,
    uint64_t seed,
    int n_nodes_hint,
    const GameConfig& config) {

    EconomyGymState state;
    state.config = config;
    state.player_id = 0;

    // Derive Poisson intensity from node count hint if provided
    GameConfig gen_config = config;
    if (n_nodes_hint > 0) {
        float area = gen_config.region_width * gen_config.region_height;
        gen_config.poisson_intensity = static_cast<float>(n_nodes_hint) / area;
    }

    // Generate a Poisson graph.  Place one capital for the test player.
    std::vector<int> capitals = {0};
    Game game(gen_config, capitals, seed);

    state.graph = game.graph();
    state.nodes = game.node_data();
    state.config = gen_config;

    // Give the player ownership of all nodes so the solver has full territory.
    for (int i = 0; i < state.graph.num_nodes(); i++) {
        state.nodes[i].owner = state.player_id;
        if (state.nodes[i].troops.empty()) {
            state.nodes[i].troops.resize(2, 0);
        }
        // Give enough troops to build anything (for greedy solver).
        state.nodes[i].troops[state.player_id] = config.cost_powerplant + 1;
    }
    // Keep node 0 as capital.
    state.nodes[0].state = NodeState::CAPITAL;

    // Run solver.
    EconomySolver solver = get_economy_solver(solver_name);
    if (solver) {
        state.plan = solver(state.graph, state.nodes, state.player_id, config);

        // Apply the plan to the state.
        for (const auto& cmd : state.plan.steps) {
            if (cmd.node_idx >= 0 && cmd.node_idx < state.graph.num_nodes()) {
                state.nodes[cmd.node_idx].state = cmd.structure;
            }
        }
    }

    // Measure results.
    state.production_rate = compute_production_rate(
        state.graph, state.nodes, state.player_id, config);
    state.theoretical_max = compute_theoretical_max_production(
        state.graph, state.nodes, state.player_id, config);
    state.efficiency = (state.theoretical_max > 0.0f)
        ? state.production_rate / state.theoretical_max : 0.0f;

    // Count buildings.
    state.n_factories = 0;
    state.n_powerplants = 0;
    for (int i = 0; i < state.graph.num_nodes(); i++) {
        if (state.nodes[i].owner == state.player_id) {
            if (state.nodes[i].state == NodeState::FACTORY) state.n_factories++;
            if (state.nodes[i].state == NodeState::POWERPLANT) state.n_powerplants++;
        }
    }

    return state;
}
