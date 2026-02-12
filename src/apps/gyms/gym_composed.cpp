#include "systems/common/buffer.hpp"
#include "systems/common/types.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "systems/ordering/ordering_solvers.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "gyms/economy_gym.hpp"
#include "engine/game.hpp"
#include "ai/players/passive_player.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Composed pipeline demo:
//   Economy → Buffer<BuildPlan> → Ordering → Buffer<DesiredDistribution> → Transport
//
// Single-player game, no opponent. Demonstrates that the isolated systems
// compose into a functioning build-and-distribute pipeline.

int main(int argc, char* argv[]) {
    std::string economy = "bootstrap";
    std::string ordering = "production_gradient";
    std::string transport = "greedy";
    uint64_t seed = 42;
    int max_ticks = 3000;

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--economy=", 10) == 0)
            economy = argv[i] + 10;
        else if (std::strncmp(argv[i], "--ordering=", 11) == 0)
            ordering = argv[i] + 11;
        else if (std::strncmp(argv[i], "--transport=", 12) == 0)
            transport = argv[i] + 12;
        else if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        else if (std::strncmp(argv[i], "--ticks=", 8) == 0)
            max_ticks = std::atoi(argv[i] + 8);
    }

    const int pid = 0;
    GameConfig config{};
    std::vector<int> capitals = {0};
    Game game(config, capitals, seed);
    int n = game.graph().num_nodes();
    int n_players = game.n_players();

    // Give player ownership of all nodes
    for (int i = 0; i < n; i++) {
        game.set_node_state(i, game.node_data()[i].state, pid,
                            config.cost_powerplant + 50);
    }

    std::printf("Composed pipeline: economy=%s, ordering=%s, transport=%s\n",
                economy.c_str(), ordering.c_str(), transport.c_str());
    std::printf("Graph: %d nodes, %d edges\n",
                game.graph().num_nodes(), game.graph().num_edges());

    // --- Phase 1: Economy produces a BuildPlan ---
    EconomySolver eco_solver = get_economy_solver(economy);
    if (!eco_solver) {
        std::fprintf(stderr, "Unknown economy solver: %s\n", economy.c_str());
        return 1;
    }
    BuildPlan plan = eco_solver(game.graph(), game.node_data(), pid, config);
    std::printf("Economy: %zu build steps, est_prod=%.1f\n",
                plan.steps.size(), plan.estimated_production);

    Buffer<BuildPlan> plan_buffer(plan);

    // --- Phase 2: Ordering produces a permutation ---
    OrderingSolver ord_solver = get_ordering_solver(ordering);
    if (!ord_solver) {
        std::fprintf(stderr, "Unknown ordering solver: %s\n", ordering.c_str());
        return 1;
    }
    std::vector<int> order = ord_solver(
        plan_buffer.read(), game.graph(), game.node_data(), pid, config);
    std::printf("Ordering: permutation of %zu steps\n", order.size());

    // --- Phase 3: Execute build plan using transport to accumulate troops ---
    TransportSolver trn_solver = get_transport_solver(transport);
    if (!trn_solver) {
        std::fprintf(stderr, "Unknown transport solver: %s\n", transport.c_str());
        return 1;
    }

    int build_cursor = 0;
    int builds_completed = 0;
    float dt = 1.0f;

    // Create passive AIs for all players (we drive commands manually)
    std::vector<PlayerCommands> commands(n_players);

    for (int tick = 0; tick < max_ticks; tick++) {
        // Reset commands
        for (auto& c : commands) c = PlayerCommands{};

        // Determine current build target
        int target_node = -1;
        NodeState target_state = NodeState::DEFAULT;
        int target_cost = 0;
        if (build_cursor < static_cast<int>(order.size())) {
            int step_idx = order[build_cursor];
            const auto& step = plan.steps[step_idx];
            target_node = step.node_idx;
            target_state = step.structure;
            target_cost = building_cost(target_state, config);
        }

        // Transport: create gradient that pulls troops toward the build target
        std::vector<float> gradient(n, 0.0f);
        if (target_node >= 0) {
            gradient[target_node] = 100.0f;
            // Mild pull to neighbors too
            for (int nb : game.graph().nodes[target_node].neighbor_indices) {
                gradient[nb] = std::max(gradient[nb], 30.0f);
            }
        }
        std::vector<bool> masked(n, false);

        auto troop_cmds = trn_solver(
            game.graph(), game.node_data(), gradient, pid, masked);
        commands[pid].troops = troop_cmds;

        // Try to build if we have enough troops at the target
        if (target_node >= 0 && game.node_data()[target_node].owner == pid) {
            int avail = game.node_data()[target_node].troops[pid];
            if (avail >= target_cost &&
                game.node_data()[target_node].state == NodeState::DEFAULT) {
                commands[pid].builds.push_back({target_node, target_state});
                build_cursor++;
                builds_completed++;
            }
        }

        game.tick(dt, commands);

        float prod = compute_production_rate(
            game.graph(), game.node_data(), pid, config);

        if (tick % 500 == 0 || build_cursor >= static_cast<int>(order.size())) {
            std::printf("  tick %4d: builds=%d/%zu, production=%.1f\n",
                        tick, builds_completed, order.size(), prod);
        }

        if (build_cursor >= static_cast<int>(order.size())) {
            std::printf("All %d builds completed at tick %d. Final production=%.1f\n",
                        builds_completed, tick, prod);
            return 0;
        }
    }

    float prod = compute_production_rate(
        game.graph(), game.node_data(), pid, config);
    std::printf("Timeout after %d ticks: %d/%zu builds completed, production=%.1f\n",
                max_ticks, builds_completed, order.size(), prod);
    return 0;
}
