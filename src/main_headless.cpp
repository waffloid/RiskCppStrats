#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

#include "engine/game.hpp"
#include "player/attention_ai.hpp"

// Passive AI for neutral player
class PassiveAI : public PlayerInterface {
public:
    void decide(const Game& /*game*/, int /*player_id*/, PlayerCommands& /*out*/) override {}
};

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    int max_ticks = 10000;
    float dt = 1.0f;

    if (argc > 1) seed = static_cast<uint64_t>(std::atoll(argv[1]));
    if (argc > 2) max_ticks = std::atoi(argv[2]);

    GameConfig config;
    config.poisson_intensity = 0.04f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 7;
    config.init_default_troops = 25;

    Graph g = Graph::generate_poisson(config, seed);
    if (g.num_nodes() < 2) {
        printf("Graph too small (%d nodes), try different seed or config\n", g.num_nodes());
        return 1;
    }

    std::vector<int> capitals = {0, 1};
    int n_real = static_cast<int>(capitals.size());

    Game game(config, capitals, seed);
    int n_total = game.n_players();
    printf("Game: %d players (+%d neutral), %d nodes, %d edges\n",
           n_real, n_total - n_real, game.graph().num_nodes(), game.graph().num_edges());

    std::vector<std::unique_ptr<PlayerInterface>> ais;
    for (int i = 0; i < n_real; i++) {
        ais.push_back(std::make_unique<AttentionAI>(i));
    }
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassiveAI>());
    }

    std::vector<PlayerCommands> commands(n_total);

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int tick = 0; tick < max_ticks; tick++) {
        for (int p = 0; p < n_total; p++) {
            commands[p] = PlayerCommands{};
            if (game.is_alive(p)) {
                ais[p]->decide(game, p, commands[p]);
            }
        }

        game.tick(dt, commands);

        if (tick % 1000 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            double tps = (tick > 0) ? tick / elapsed : 0;
            printf("tick %5d (%.1f ticks/sec): ", tick, tps);
            for (int p = 0; p < n_real; p++) {
                int total = 0, nodes_owned = 0;
                for (const auto& nd : game.node_data()) {
                    total += nd.troops[p];
                    if (nd.owner == p) nodes_owned++;
                }
                printf("P%d: %d troops, %d nodes%s  ",
                       p, total, nodes_owned, game.is_alive(p) ? "" : " (dead)");
            }
            printf("\n");
        }

        if (game.is_game_over()) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - t0).count();
            printf("Game over at tick %d (%.3fs, %.0f ticks/sec)\n", tick, elapsed, tick / elapsed);
            for (int p = 0; p < n_real; p++) {
                if (game.is_alive(p)) printf("Winner: P%d\n", p);
            }
            return 0;
        }
    }

    auto now = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(now - t0).count();
    printf("Did not finish in %d ticks (%.3fs, %.0f ticks/sec)\n", max_ticks, elapsed, max_ticks / elapsed);
    return 0;
}
