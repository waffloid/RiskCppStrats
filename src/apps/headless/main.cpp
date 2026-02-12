#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "engine/game.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/models.hpp"
#include "ai/players/passive_player.hpp"

static bool json_mode = false;

static void print_json_tick(const Game& game, int tick, int n_real) {
    std::printf("{\"tick\":%d,\"players\":[", tick);
    for (int p = 0; p < n_real; p++) {
        int total_troops = 0, nodes_owned = 0;
        for (const auto& nd : game.node_data()) {
            if (p < static_cast<int>(nd.troops.size())) total_troops += nd.troops[p];
            if (nd.owner == p) nodes_owned++;
        }
        if (p > 0) std::printf(",");
        std::printf("{\"id\":%d,\"troops\":%d,\"nodes\":%d,\"alive\":%s}",
                    p, total_troops, nodes_owned,
                    game.is_alive(p) ? "true" : "false");
    }
    std::printf("],\"game_over\":%s}\n", game.is_game_over() ? "true" : "false");
}

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    int max_ticks = 10000;
    float dt = 1.0f;

    // Check for --json flag (can appear anywhere in args)
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--json") {
            json_mode = true;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() > 0) seed = static_cast<uint64_t>(std::atoll(positional[0].c_str()));
    if (positional.size() > 1) max_ticks = std::atoi(positional[1].c_str());

    std::string model_p0 = "v0_expansion";
    std::string model_p1 = "v0_expansion";
    if (positional.size() > 2) model_p0 = positional[2];
    if (positional.size() > 3) model_p1 = positional[3];

    // Validate model names
    const ModelFactory* factory_p0 = get_model(model_p0);
    const ModelFactory* factory_p1 = get_model(model_p1);
    if (!factory_p0 || !factory_p1) {
        if (!factory_p0) fprintf(stderr, "Unknown model: %s\n", model_p0.c_str());
        if (!factory_p1) fprintf(stderr, "Unknown model: %s\n", model_p1.c_str());
        fprintf(stderr, "Available models:");
        for (const auto& name : list_models()) fprintf(stderr, " %s", name.c_str());
        fprintf(stderr, "\nUsage: %s [--json] [seed] [max_ticks] [model_p0] [model_p1]\n", argv[0]);
        return 1;
    }

    GameConfig config;
    config.poisson_intensity = 0.04f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 7;
    config.init_default_troops = 25;

    Graph g = Graph::generate_poisson(config, seed);
    if (g.num_nodes() < 2) {
        fprintf(stderr, "Graph too small (%d nodes), try different seed or config\n", g.num_nodes());
        return 1;
    }

    std::vector<int> capitals = {0, 1};
    int n_real = static_cast<int>(capitals.size());

    Game game(config, capitals, seed);
    int n_total = game.n_players();

    if (!json_mode) {
        printf("Game: %d players (+%d neutral), %d nodes, %d edges\n",
               n_real, n_total - n_real, game.graph().num_nodes(), game.graph().num_edges());
        printf("P0: %s  vs  P1: %s\n", model_p0.c_str(), model_p1.c_str());
    }

    std::vector<std::unique_ptr<PlayerInterface>> ais;
    ais.push_back((*factory_p0)(0));
    ais.push_back((*factory_p1)(1));
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassivePlayer>());
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

        if (json_mode) {
            // Output every 100 ticks and on game over
            if (tick % 100 == 0 || game.is_game_over()) {
                print_json_tick(game, tick, n_real);
            }
        } else if (tick % 1000 == 0) {
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
            if (!json_mode) {
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - t0).count();
                printf("Game over at tick %d (%.3fs, %.0f ticks/sec)\n", tick, elapsed, tick / elapsed);
                for (int p = 0; p < n_real; p++) {
                    if (game.is_alive(p)) printf("Winner: P%d (%s)\n", p, (p == 0 ? model_p0 : model_p1).c_str());
                }
            }
            return 0;
        }
    }

    if (!json_mode) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        printf("Did not finish in %d ticks (%.3fs, %.0f ticks/sec)\n", max_ticks, elapsed, max_ticks / elapsed);
    }
    return 0;
}
