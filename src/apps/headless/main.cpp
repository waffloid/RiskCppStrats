#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#include "engine/game.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/models.hpp"
#include "ai/players/passive_player.hpp"

#include "ai/models/model_registry.hpp"

static bool json_mode = false;
static bool diag_mode = false;
static int diag_interval = 50;

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

static void print_diag_tick(const Game& game, int tick, int n_real,
                            const std::vector<std::unique_ptr<PlayerInterface>>& ais,
                            const std::vector<PlayerCommands>& commands) {
    const auto& nodes = game.node_data();
    int n = game.graph().num_nodes();

    printf("--- tick %d ---\n", tick);
    for (int p = 0; p < n_real; p++) {
        int total_troops = 0, nodes_owned = 0;
        int factories = 0, powerplants = 0, forts = 0, artillery = 0;
        int max_troops_node = -1, max_troops_count = 0;

        for (int i = 0; i < n; i++) {
            const auto& nd = nodes[i];
            if (nd.owner == p) {
                nodes_owned++;
                total_troops += nd.troops[p];
                if (nd.troops[p] > max_troops_count) {
                    max_troops_count = nd.troops[p];
                    max_troops_node = i;
                }
                switch (nd.state) {
                    case NodeState::FACTORY:    factories++; break;
                    case NodeState::POWERPLANT: powerplants++; break;
                    case NodeState::FORT:       forts++; break;
                    case NodeState::ARTILLERY:  artillery++; break;
                    default: break;
                }
            }
        }

        printf("  P%d: %d troops, %d nodes, %dF/%dPP",
               p, total_troops, nodes_owned, factories, powerplants);
        if (forts > 0) printf("/%dFt", forts);
        if (artillery > 0) printf("/%dArt", artillery);
        printf(" | max=%d@n%d", max_troops_count, max_troops_node);

        // Show build commands this tick
        int n_builds = static_cast<int>(commands[p].builds.size());
        int n_troop_cmds = static_cast<int>(commands[p].troops.size());
        if (n_builds > 0) {
            printf(" | BUILDS:");
            for (const auto& b : commands[p].builds) {
                const char* type = "?";
                switch (b.structure) {
                    case NodeState::FACTORY:    type = "F"; break;
                    case NodeState::POWERPLANT: type = "PP"; break;
                    case NodeState::FORT:       type = "Ft"; break;
                    case NodeState::ARTILLERY:  type = "Art"; break;
                    default: break;
                }
                printf(" %s@n%d", type, b.node_idx);
            }
        }
        printf(" | %d moves", n_troop_cmds);

        // Show AI distribution metrics if available
        auto* dist_player = dynamic_cast<DistributionAIPlayer*>(ais[p].get());
        if (dist_player) {
            const auto& snap = dist_player->decision_snapshot();
            const auto& metrics = dist_player->metrics();

            printf(" | H=%.2f HHI=%.3f util=%.0f%%",
                   metrics.distribution_entropy,
                   metrics.distribution_concentration,
                   metrics.distribution_gradient_util * 100.0f);

            // Show top 3 nodes in distribution
            if (!snap.smoothed.empty()) {
                std::vector<std::pair<float, int>> sorted_dist;
                for (int i = 0; i < static_cast<int>(snap.smoothed.size()); i++) {
                    sorted_dist.push_back({snap.smoothed[i], i});
                }
                std::sort(sorted_dist.rbegin(), sorted_dist.rend());
                printf(" | top3:");
                for (int k = 0; k < 3 && k < static_cast<int>(sorted_dist.size()); k++) {
                    printf(" n%d=%.1f%%", sorted_dist[k].second, sorted_dist[k].first * 100.0f);
                }
            }
        }
        printf("\n");
    }
}

int main(int argc, char* argv[]) {
    register_graph_algo_models();

    uint64_t seed = 42;
    int max_ticks = 10000;
    float dt = 0.25f;

    // Check for flags (can appear anywhere in args)
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--json") {
            json_mode = true;
        } else if (std::string(argv[i]) == "--diag") {
            diag_mode = true;
        } else if (std::string(argv[i]).rfind("--diag=", 0) == 0) {
            diag_mode = true;
            diag_interval = std::atoi(argv[i] + 7);
            if (diag_interval < 1) diag_interval = 1;
        } else if (std::string(argv[i]).rfind("--dt=", 0) == 0) {
            dt = std::atof(argv[i] + 5);
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() > 0) seed = static_cast<uint64_t>(std::atoll(positional[0].c_str()));
    if (positional.size() > 1) max_ticks = std::atoi(positional[1].c_str());

    // Collect model names from positional args (after seed and max_ticks)
    std::vector<std::string> model_names;
    for (size_t i = 2; i < positional.size(); i++) {
        model_names.push_back(positional[i]);
    }
    if (model_names.empty()) model_names = {"v0_expansion", "v0_expansion"};
    if (model_names.size() < 2) model_names.push_back("v0_expansion");

    // Validate model names
    std::vector<const ModelFactory*> model_factories;
    for (const auto& name : model_names) {
        const ModelFactory* f = get_model(name);
        if (!f) {
            fprintf(stderr, "Unknown model: %s\n", name.c_str());
            fprintf(stderr, "Available models:");
            for (const auto& m : list_models()) fprintf(stderr, " %s", m.c_str());
            fprintf(stderr, "\n");
            return 1;
        }
        model_factories.push_back(f);
    }

    GameConfig config;
    config.poisson_intensity = 0.16f;
    config.region_width = 158.0f;
    config.region_height = 158.0f;
    config.edge_distance_threshold = 10.0f;
    config.max_neighbors = 7;
    config.circular = true;
    config.num_holes = 6;
    config.hole_radius_min = 8.0f;
    config.hole_radius_max = 18.0f;
    config.init_default_troops = 25;

    Graph g = Graph::generate_poisson(config, seed);
    if (g.num_nodes() < 2) {
        fprintf(stderr, "Graph too small (%d nodes), try different seed or config\n", g.num_nodes());
        return 1;
    }

    int n_real = static_cast<int>(model_names.size());
    auto capitals = g.pick_spaced_capitals(n_real);

    Game game(config, capitals, seed);
    int n_total = game.n_players();

    if (!json_mode) {
        printf("Game: %d players (+%d neutral), %d nodes, %d edges\n",
               n_real, n_total - n_real, game.graph().num_nodes(), game.graph().num_edges());
        for (int i = 0; i < n_real; i++) printf("P%d: %s\n", i, model_names[i].c_str());
    }

    std::vector<std::unique_ptr<PlayerInterface>> ais;
    for (int i = 0; i < n_real; i++) {
        ais.push_back((*model_factories[i])(i));
    }
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassivePlayer>());
    }

    // Enable metrics for diag mode
    if (diag_mode) {
        for (int p = 0; p < n_real; p++) {
            auto* dist_player = dynamic_cast<DistributionAIPlayer*>(ais[p].get());
            if (dist_player) dist_player->set_metrics_enabled(true);
        }
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

        if (diag_mode && (tick % diag_interval == 0 || game.is_game_over())) {
            print_diag_tick(game, tick, n_real, ais, commands);
        }

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
                printf("tick %5d (%.1f ticks/sec): ", tick, tick / elapsed);
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
                printf("Game over at tick %d (%.3fs, %.0f ticks/sec)\n", tick, elapsed, tick / elapsed);
                for (int p = 0; p < n_real; p++) {
                    if (game.is_alive(p)) printf("Winner: P%d (%s)\n", p, model_names[p].c_str());
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
