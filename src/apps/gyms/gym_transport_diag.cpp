// Diagnostic: trace per-node state on path_end to identify oscillation
#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "engine/game.hpp"

#include <cstdio>
#include <cmath>
#include <random>

int main() {
    auto preset = get_transport_preset("path_end");
    if (!preset) { std::fprintf(stderr, "bad preset\n"); return 1; }

    int n = preset->graph.num_nodes();
    int max_ticks = 200;

    // Zero-production game
    GameConfig config;
    config.init_troop_count = 0;
    config.capital_troops_per_tick = 0;
    config.factory_troops_per_tick = 0;
    config.powerplant_bonus = 0;

    Game game(config, preset->graph, {0});
    for (int i = 0; i < n; i++) {
        NodeState state = (i == 0) ? NodeState::CAPITAL : NodeState::DEFAULT;
        game.set_node_state(i, state, 0, preset->initial_troops[i]);
    }

    std::vector<bool> no_mask(n, false);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> coin(0.0f, 1.0f);

    // Header
    std::printf("tick");
    for (int i = 0; i < n; i++) std::printf(",node%d", i);
    for (int i = 0; i < n; i++) std::printf(",transit_from%d", i);
    for (int i = 0; i < n; i++) std::printf(",pot%d", i);
    std::printf(",loss\n");

    for (int tick = 0; tick < max_ticks; tick++) {
        // Count on-node, in-transit (at origin), committed
        std::vector<int> on_node(n);
        std::vector<int> current(n);
        std::vector<int> committed(n, 0);
        for (int i = 0; i < n; i++) {
            on_node[i] = game.node_data()[i].troops[0];
            current[i] = on_node[i];
        }
        std::vector<int> in_transit_from(n, 0);
        for (const auto& el : game.edge_lanes()) {
            for (int lane = 0; lane < 2; lane++) {
                int origin = (lane == 0) ? el.node_a : el.node_b;
                for (const auto& g : el.lanes[lane].groups) {
                    if (g.owner != 0) continue;
                    current[origin] += g.count;
                    in_transit_from[origin] += g.count;
                    if (!g.retreating) committed[origin] += g.count;
                }
            }
        }

        // Gradient & loss
        std::vector<float> gradient(n);
        float loss = 0.0f;
        for (int i = 0; i < n; i++) {
            gradient[i] = static_cast<float>(preset->target_troops[i] - current[i]);
            loss += std::abs(gradient[i]);
        }

        // Print state
        std::printf("%d", tick);
        for (int i = 0; i < n; i++) std::printf(",%d", on_node[i]);
        for (int i = 0; i < n; i++) std::printf(",%d", in_transit_from[i]);
        for (int i = 0; i < n; i++) std::printf(",%d", static_cast<int>(gradient[i]));
        std::printf(",%.0f\n", loss);

        // Solver
        auto commands = transport_solver_greedy(
            game.graph(), game.node_data(), gradient, 0, no_mask, 0.1f, 1);

        // Apply committed cap (same as gym)
        std::vector<int> sendable(n);
        for (int i = 0; i < n; i++) {
            int surplus = current[i] - preset->target_troops[i];
            sendable[i] = std::max(0, surplus - committed[i]);
        }
        for (auto& cmd : commands) {
            if (sendable[cmd.from_node] <= 0) {
                cmd.count = 0;
            } else {
                if (cmd.count > sendable[cmd.from_node])
                    cmd.count = sendable[cmd.from_node];
                sendable[cmd.from_node] -= cmd.count;
            }
        }
        commands.erase(
            std::remove_if(commands.begin(), commands.end(),
                [](const TroopCommand& c) { return c.count <= 0; }),
            commands.end());

        // Stochastic 1-troop sends (same as gym)
        for (int i = 0; i < n; i++) {
            if (sendable[i] <= 0) continue;
            if (game.node_data()[i].troops[0] < 2) continue;
            float prob = static_cast<float>(sendable[i]) /
                         static_cast<float>(std::max(1, preset->target_troops[i]));
            if (coin(rng) >= prob) continue;
            int best_nb = -1;
            float best_grad = -1e18f;
            for (int nb : game.graph().neighbors(i)) {
                if (gradient[nb] > best_grad) {
                    best_grad = gradient[nb];
                    best_nb = nb;
                }
            }
            if (best_nb >= 0 && best_grad > gradient[i]) {
                commands.push_back({i, best_nb, 1});
            }
        }

        std::vector<PlayerCommands> all_commands(game.n_players());
        all_commands[0].troops = std::move(commands);
        game.tick(1.0f, all_commands);
    }

    return 0;
}
