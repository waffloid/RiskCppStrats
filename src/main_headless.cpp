#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/game.hpp"

// Stub AI: does nothing (just holds territory via production)
class PassiveAI : public PlayerInterface {
public:
    void decide(const Game& /*game*/, int /*player_id*/, PlayerCommands& /*out*/) override {
        // No-op: rely on capital production only
    }
};

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    int max_ticks = 10000;
    float dt = 1.0f;

    if (argc > 1) seed = static_cast<uint64_t>(std::atoll(argv[1]));
    if (argc > 2) max_ticks = std::atoi(argv[2]);

    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 6;

    // Generate graph to figure out valid capital placements
    Graph g = Graph::generate_poisson(config, seed);
    if (g.num_nodes() < 2) {
        printf("Graph too small (%d nodes), try different seed or config\n", g.num_nodes());
        return 1;
    }

    // Place capitals at nodes 0 and 1
    std::vector<int> capitals = {0, 1};
    int n_players = static_cast<int>(capitals.size());

    Game game(config, capitals, seed);
    printf("Game started: %d players, %d nodes, %d edges\n",
           n_players, game.graph().num_nodes(), game.graph().num_edges());

    std::vector<std::unique_ptr<PlayerInterface>> ais;
    for (int i = 0; i < n_players; i++) {
        ais.push_back(std::make_unique<PassiveAI>());
    }

    std::vector<PlayerCommands> commands(n_players);

    for (int tick = 0; tick < max_ticks; tick++) {
        // Collect AI decisions
        for (int p = 0; p < n_players; p++) {
            commands[p] = PlayerCommands{};
            if (game.is_alive(p)) {
                ais[p]->decide(game, p, commands[p]);
            }
        }

        game.tick(dt, commands);

        if (tick % 1000 == 0) {
            printf("tick %d: ", tick);
            for (int p = 0; p < n_players; p++) {
                int total = 0;
                for (const auto& nd : game.node_data()) {
                    total += nd.troops[p];
                }
                printf("P%d=%d%s ", p, total, game.is_alive(p) ? "" : "(dead)");
            }
            printf("\n");
        }

        if (game.is_game_over()) {
            printf("Game over at tick %d\n", tick);
            for (int p = 0; p < n_players; p++) {
                if (game.is_alive(p)) {
                    printf("Winner: P%d\n", p);
                }
            }
            return 0;
        }
    }

    printf("Game did not finish in %d ticks\n", max_ticks);
    return 0;
}
