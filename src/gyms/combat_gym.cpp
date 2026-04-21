#include "gyms/combat_gym.hpp"
#include "systems/combat/combat_solvers.hpp"
#include "engine/game.hpp"
#include "ai/players/passive_player.hpp"

// --- Shared per-tick simulation ---

CombatTickMetrics combat_gym_tick(
    Game& game,
    const std::vector<PlayerInterface*>& players,
    float dt) {

    int n_total = game.n_players();
    int n_real = game.n_real_players();

    // Decide for each alive player
    std::vector<PlayerCommands> commands(n_total);
    for (int p = 0; p < n_total; p++) {
        if (!game.is_alive(p)) continue;
        if (p < static_cast<int>(players.size()) && players[p])
            players[p]->decide(game, p, commands[p]);
    }

    // Pure combat — no building
    for (auto& c : commands) c.builds.clear();

    game.tick(dt, commands);

    // Extract per-tick metrics
    CombatTickMetrics metrics;
    metrics.troops.resize(n_real, 0);
    metrics.territory.resize(n_real, 0.0f);
    metrics.deaths.resize(n_real, 0);

    // Deaths this tick
    const auto& deaths = game.tick_deaths();
    for (int p = 0; p < n_real && p < static_cast<int>(deaths.size()); p++) {
        metrics.deaths[p] = deaths[p];
    }

    // Count on-node troops and territory (fractional ownership by troop share)
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        int total_at_node = 0;
        for (int p = 0; p < n_real; p++) {
            int t = (p < static_cast<int>(nd.troops.size())) ? nd.troops[p] : 0;
            metrics.troops[p] += t;
            total_at_node += t;
        }
        if (total_at_node > 0) {
            for (int p = 0; p < n_real; p++) {
                int t = (p < static_cast<int>(nd.troops.size())) ? nd.troops[p] : 0;
                metrics.territory[p] += static_cast<float>(t) / static_cast<float>(total_at_node);
            }
        }
    }

    // Count in-transit troops
    for (const auto& el : game.edge_lanes()) {
        for (int lane = 0; lane < 2; lane++) {
            for (const auto& g : el.lanes[lane].groups) {
                if (g.owner >= 0 && g.owner < n_real)
                    metrics.troops[g.owner] += g.count;
            }
        }
    }

    return metrics;
}

// --- Gym runner ---

CombatGymResult run_combat_gym(
    const CombatBenchmark& benchmark,
    const std::string& solver_name,
    const std::string& opponent_name,
    float spartan_multiplier,
    bool collect_ticks) {

    CombatGymResult result;
    result.benchmark = benchmark.name;
    result.solver = solver_name;
    result.opponent = opponent_name;
    result.spartan_multiplier = spartan_multiplier;

    // Create players from model registry
    const CombatSolver* solver_factory = get_combat_solver(solver_name);
    const CombatSolver* opponent_factory = get_combat_solver(opponent_name);
    if (!solver_factory || !opponent_factory) return result;

    auto solver_ai = (*solver_factory)(0);
    auto opponent_ai = (*opponent_factory)(1);
    PassivePlayer passive;

    // Construct game from benchmark specification
    Game game(benchmark.config, benchmark.graph, benchmark.capitals);

    for (const auto& ov : benchmark.overrides) {
        game.set_node_state(ov.node_idx, ov.state, ov.owner, ov.troops);
    }

    // Spartan multiplier: scale all opponent-owned troops
    if (spartan_multiplier != 1.0f) {
        for (int i = 0; i < game.graph().num_nodes(); i++) {
            const auto& nd = game.node_data()[i];
            if (nd.owner == 1 && 1 < static_cast<int>(nd.troops.size())) {
                int scaled = static_cast<int>(nd.troops[1] * spartan_multiplier);
                game.set_node_state(i, nd.state, nd.owner, scaled);
            }
        }
    }

    // Build player pointer vector for combat_gym_tick
    int n_total = game.n_players();
    int n_real = game.n_real_players();
    std::vector<PlayerInterface*> players(n_total, &passive);
    players[0] = solver_ai.get();
    if (n_real > 1) players[1] = opponent_ai.get();

    // Cumulative kill/death counters for per-tick records
    std::vector<int> cum_kills(n_real, 0);
    std::vector<int> cum_deaths(n_real, 0);

    if (collect_ticks) {
        result.tick_records.reserve(benchmark.max_ticks);
    }

    for (int tick = 0; tick < benchmark.max_ticks; tick++) {
        auto metrics = combat_gym_tick(game, players);

        // Accumulate per-player deaths
        if (0 < static_cast<int>(metrics.deaths.size())) result.deaths_p0 += metrics.deaths[0];
        if (1 < static_cast<int>(metrics.deaths.size())) result.deaths_p1 += metrics.deaths[1];

        // Collect per-tick data
        if (collect_ticks) {
            for (int p = 0; p < n_real && p < static_cast<int>(metrics.deaths.size()); p++) {
                cum_deaths[p] += metrics.deaths[p];
                if (n_real == 2) {
                    cum_kills[1 - p] += metrics.deaths[p];
                }
            }

            CombatTickRecord rec;
            rec.tick = tick;
            rec.players.resize(n_real);

            for (int p = 0; p < n_real; p++) {
                auto& pt = rec.players[p];
                pt.cumulative_kills = cum_kills[p];
                pt.cumulative_deaths = cum_deaths[p];
                pt.kills_this_tick = (n_real == 2) ? metrics.deaths[1 - p] : 0;
                pt.deaths_this_tick = metrics.deaths[p];
                pt.kd_ratio = (pt.cumulative_deaths > 0)
                    ? static_cast<float>(pt.cumulative_kills) / static_cast<float>(pt.cumulative_deaths)
                    : 0.0f;
                pt.troops_on_nodes = 0;
                pt.nodes_owned = 0;
                for (int i = 0; i < game.graph().num_nodes(); i++) {
                    const auto& nd = game.node_data()[i];
                    if (nd.owner == p) pt.nodes_owned++;
                    if (p < static_cast<int>(nd.troops.size()))
                        pt.troops_on_nodes += nd.troops[p];
                }
                pt.troops_in_transit = metrics.troops[p] - pt.troops_on_nodes;
                pt.troops_total = metrics.troops[p];
            }

            result.tick_records.push_back(std::move(rec));
        }

        if (game.is_game_over()) {
            result.ticks_elapsed = tick + 1;
            break;
        }
    }

    if (result.ticks_elapsed == 0) result.ticks_elapsed = benchmark.max_ticks;

    // Final stats from last tick's approach: just count nodes
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        if (game.node_data()[i].owner == 0) result.nodes_p0++;
        if (game.node_data()[i].owner == 1) result.nodes_p1++;
    }
    // Use the same troop counting that combat_gym_tick uses
    std::vector<PlayerInterface*> dummy;  // won't be used
    // Just count troops directly for final state
    result.troops_p0 = 0;
    result.troops_p1 = 0;
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        if (0 < static_cast<int>(nd.troops.size())) result.troops_p0 += nd.troops[0];
        if (1 < static_cast<int>(nd.troops.size())) result.troops_p1 += nd.troops[1];
    }
    for (const auto& el : game.edge_lanes()) {
        for (int lane = 0; lane < 2; lane++) {
            for (const auto& g : el.lanes[lane].groups) {
                if (g.owner == 0) result.troops_p0 += g.count;
                if (g.owner == 1) result.troops_p1 += g.count;
            }
        }
    }

    // Determine winner
    if (game.is_game_over()) {
        if (game.is_alive(0) && !game.is_alive(1)) result.winner = 0;
        else if (!game.is_alive(0) && game.is_alive(1)) result.winner = 1;
        else result.winner = -1;
    } else {
        // Timeout — decide by territory
        if (result.nodes_p0 > result.nodes_p1) result.winner = 0;
        else if (result.nodes_p1 > result.nodes_p0) result.winner = 1;
        else result.winner = -1;
    }

    return result;
}
