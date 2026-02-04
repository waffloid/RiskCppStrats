#include "engine/benchmark.hpp"
#include "player/passive_ai.hpp"

ScenarioResult run_scenario(
    const GameConfig& config,
    Graph graph,
    const std::vector<int>& capitals,
    const std::vector<NodeOverride>& overrides,
    PlayerInterface& test_ai,
    int test_player_id,
    PlayerInterface& opponent_ai,
    int max_ticks,
    float dt) {

    Game game(config, std::move(graph), capitals);

    // Apply node overrides
    for (const auto& ov : overrides) {
        game.set_node_state(ov.node_idx, ov.state, ov.owner, ov.troops);
    }

    int n_total = game.n_players();
    PassiveAI passive;

    std::vector<PlayerCommands> commands(n_total);
    int tick = 0;

    for (; tick < max_ticks; tick++) {
        // Clear commands
        for (auto& c : commands) {
            c = PlayerCommands{};
        }

        // Get decisions
        for (int p = 0; p < n_total; p++) {
            if (!game.is_alive(p)) continue;

            if (p == test_player_id) {
                test_ai.decide(game, p, commands[p]);
            } else if (p < game.n_real_players()) {
                opponent_ai.decide(game, p, commands[p]);
            } else {
                // Neutral player
                passive.decide(game, p, commands[p]);
            }
        }

        game.tick(dt, commands);

        if (game.is_game_over()) {
            tick++;
            break;
        }
    }

    // Compute results
    ScenarioResult result;
    result.ticks_elapsed = tick;

    // Count nodes owned by test player
    result.nodes_captured = 0;
    result.total_troops = 0;
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        if (nd.owner == test_player_id) {
            result.nodes_captured++;
        }
        if (test_player_id < static_cast<int>(nd.troops.size())) {
            result.total_troops += nd.troops[test_player_id];
        }
    }

    // Count troops in transit
    for (const auto& el : game.edge_lanes()) {
        for (int lane = 0; lane < 2; lane++) {
            for (const auto& g : el.lanes[lane].groups) {
                if (g.owner == test_player_id) {
                    result.total_troops += g.count;
                }
            }
        }
    }

    result.won = game.is_game_over() && game.is_alive(test_player_id);

    return result;
}
