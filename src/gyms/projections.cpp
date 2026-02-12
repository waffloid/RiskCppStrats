#include "gyms/projections.hpp"
#include "systems/economy/economy_solvers.hpp"

EconomyGymState project_economy(const Game& game, int player_id,
                                 const std::string& solver_name) {
    EconomyGymState state;
    state.graph = game.graph();
    state.nodes = game.node_data();
    state.player_id = player_id;
    state.config = game.config();

    // Run solver on live game state
    EconomySolver solver = get_economy_solver(solver_name);
    if (solver) {
        state.plan = solver(state.graph, state.nodes, player_id, state.config);
    }

    // Compute production metrics from current state
    state.production_rate = compute_production_rate(
        state.graph, state.nodes, player_id, state.config);
    state.theoretical_max = compute_theoretical_max_production(
        state.graph, state.nodes, player_id, state.config);
    state.efficiency = (state.theoretical_max > 0.0f)
                           ? state.production_rate / state.theoretical_max
                           : 0.0f;

    // Count buildings
    for (const auto& nd : state.nodes) {
        if (nd.owner == player_id) {
            if (nd.state == NodeState::FACTORY) state.n_factories++;
            if (nd.state == NodeState::POWERPLANT) state.n_powerplants++;
        }
    }

    return state;
}

CombatBenchmark project_combat(const Game& game) {
    CombatBenchmark b;
    b.name = "projected";
    b.graph = game.graph();
    b.config = game.config();
    b.max_ticks = 5000;

    // Find real player capitals (nodes with most troops per player)
    int n_real = game.n_real_players();
    b.capitals.resize(n_real);
    std::vector<int> best_troops(n_real, -1);

    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        if (nd.owner >= 0 && nd.owner < n_real) {
            int t = nd.troops[nd.owner];
            if (t > best_troops[nd.owner]) {
                best_troops[nd.owner] = t;
                b.capitals[nd.owner] = i;
            }
        }
    }

    // Add overrides for all non-capital nodes to reproduce current state
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        bool is_capital = false;
        for (int c : b.capitals) {
            if (c == i) { is_capital = true; break; }
        }
        if (!is_capital && nd.owner >= 0 && nd.owner < n_real) {
            int troops = nd.troops[nd.owner];
            b.overrides.push_back({i, nd.state, nd.owner, troops});
        }
    }

    return b;
}

TransportPreset project_transport(const Game& game, int player_id,
                                   const std::vector<int>& target_troops) {
    TransportPreset preset;
    preset.name = "projected";
    preset.graph = game.graph();

    int n = game.graph().num_nodes();
    preset.initial_troops.resize(n, 0);
    preset.target_troops = target_troops;

    for (int i = 0; i < n; i++) {
        const auto& nd = game.node_data()[i];
        if (nd.owner == player_id) {
            preset.initial_troops[i] = nd.troops[player_id];
        }
    }

    return preset;
}
