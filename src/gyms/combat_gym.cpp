#include "gyms/combat_gym.hpp"
#include "systems/combat/combat_solvers.hpp"
#include "engine/game.hpp"
#include "ai/players/passive_player.hpp"

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

    // Game loop — pure combat, no building
    int n_total = game.n_players();
    int n_real = game.n_real_players();
    std::vector<PlayerCommands> commands(n_total);
    PassivePlayer passive;

    // Cumulative kill/death counters for per-tick records
    std::vector<int> cum_kills(n_real, 0);
    std::vector<int> cum_deaths(n_real, 0);

    if (collect_ticks) {
        result.tick_records.reserve(benchmark.max_ticks);
    }

    for (int tick = 0; tick < benchmark.max_ticks; tick++) {
        for (auto& c : commands) c = PlayerCommands{};

        for (int p = 0; p < n_total; p++) {
            if (!game.is_alive(p)) continue;

            if (p == 0)
                solver_ai->decide(game, p, commands[p]);
            else if (p < n_real)
                opponent_ai->decide(game, p, commands[p]);
            else
                passive.decide(game, p, commands[p]);
        }

        // Filter build commands — combat gym is pure PvP
        for (auto& c : commands) c.builds.clear();

        game.tick(1.0f, commands);

        // Accumulate per-player deaths
        const auto& deaths = game.tick_deaths();
        if (0 < static_cast<int>(deaths.size())) result.deaths_p0 += deaths[0];
        if (1 < static_cast<int>(deaths.size())) result.deaths_p1 += deaths[1];

        // Collect per-tick data
        if (collect_ticks) {
            // Update cumulative counters (deaths for p0 = kills for p1 and vice versa)
            for (int p = 0; p < n_real && p < static_cast<int>(deaths.size()); p++) {
                cum_deaths[p] += deaths[p];
                // In a 2-player game, my deaths are the other player's kills
                if (n_real == 2) {
                    cum_kills[1 - p] += deaths[p];
                }
            }

            CombatTickRecord rec;
            rec.tick = tick;
            rec.players.resize(n_real);

            for (int p = 0; p < n_real; p++) {
                auto& pt = rec.players[p];
                pt.cumulative_kills = cum_kills[p];
                pt.cumulative_deaths = cum_deaths[p];
                pt.kills_this_tick = (p < static_cast<int>(deaths.size()) && n_real == 2)
                    ? deaths[1 - p] : 0;
                pt.deaths_this_tick = (p < static_cast<int>(deaths.size()))
                    ? deaths[p] : 0;
                pt.kd_ratio = (pt.cumulative_deaths > 0)
                    ? static_cast<float>(pt.cumulative_kills) / static_cast<float>(pt.cumulative_deaths)
                    : 0.0f;

                // Count on-node troops and territory
                pt.troops_on_nodes = 0;
                pt.nodes_owned = 0;
                for (int i = 0; i < game.graph().num_nodes(); i++) {
                    const auto& nd = game.node_data()[i];
                    if (nd.owner == p) pt.nodes_owned++;
                    if (p < static_cast<int>(nd.troops.size()))
                        pt.troops_on_nodes += nd.troops[p];
                }

                // Count in-transit troops
                pt.troops_in_transit = 0;
                for (const auto& el : game.edge_lanes()) {
                    for (int lane = 0; lane < 2; lane++) {
                        for (const auto& g : el.lanes[lane].groups) {
                            if (g.owner == p) pt.troops_in_transit += g.count;
                        }
                    }
                }

                pt.troops_total = pt.troops_on_nodes + pt.troops_in_transit;
            }

            result.tick_records.push_back(std::move(rec));
        }

        if (game.is_game_over()) {
            result.ticks_elapsed = tick + 1;
            break;
        }
    }

    if (result.ticks_elapsed == 0) result.ticks_elapsed = benchmark.max_ticks;

    // Count final nodes and on-node troops
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        if (nd.owner == 0) result.nodes_p0++;
        if (nd.owner == 1) result.nodes_p1++;
        if (0 < static_cast<int>(nd.troops.size())) result.troops_p0 += nd.troops[0];
        if (1 < static_cast<int>(nd.troops.size())) result.troops_p1 += nd.troops[1];
    }

    // Count troops in transit
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
