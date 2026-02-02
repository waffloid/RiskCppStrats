#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"

// Simple AI: sends troops to neighboring nodes, builds factories when affordable
class SimpleAI : public PlayerInterface {
public:
    void decide(const Game& game, int player_id, PlayerCommands& out) override {
        const auto& graph = game.graph();
        const auto& nodes = game.node_data();

        for (int i = 0; i < graph.num_nodes(); i++) {
            const auto& nd = nodes[i];
            if (nd.owner != player_id) continue;
            int troops = nd.troops[player_id];

            // Build a factory if we can afford it and node is default
            if (nd.state == NodeState::DEFAULT && troops >= game.config().cost_factory + 50) {
                out.builds.push_back({i, NodeState::FACTORY});
                troops -= game.config().cost_factory;
            }

            // Send troops to neighbors, keeping a small garrison
            int garrison = 30;
            if (troops <= garrison) continue;
            int to_send = troops - garrison;

            // Pick a neighbor: prefer unowned or enemy nodes
            const auto& node = graph.nodes[i];
            int best_nb = -1;
            int best_priority = -1;
            for (int nb : node.neighbor_indices) {
                const auto& nb_data = nodes[nb];
                int priority = 0;
                if (nb_data.owner == player_id) {
                    priority = 0; // already ours, low priority
                } else if (nb_data.owner < 0) {
                    priority = 1; // unowned
                } else {
                    priority = 2; // enemy
                }
                if (priority > best_priority) {
                    best_priority = priority;
                    best_nb = nb;
                }
            }

            if (best_nb >= 0 && to_send > 0) {
                out.troops.push_back({i, best_nb, to_send});
            }
        }
    }
};

// Passive AI: does nothing (used for neutral player)
class PassiveAI : public PlayerInterface {
public:
    void decide(const Game& /*game*/, int /*player_id*/, PlayerCommands& /*out*/) override {}
};

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    if (argc > 1) seed = static_cast<uint64_t>(std::atoll(argv[1]));

    const int screen_w = 1280;
    const int screen_h = 800;

    // Game setup
    GameConfig config;
    config.poisson_intensity = 0.04f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 7;
    config.init_default_troops = 25;

    Graph test_graph = Graph::generate_poisson(config, seed);
    if (test_graph.num_nodes() < 2) {
        printf("Graph too small (%d nodes), try different seed\n", test_graph.num_nodes());
        return 1;
    }

    std::vector<int> capitals = {0, 1};
    int n_real = static_cast<int>(capitals.size());

    Game game(config, capitals, seed);
    int n_total = game.n_players(); // includes neutral if present
    printf("Game: %d real players (+%d neutral), %d nodes, %d edges\n",
           n_real, n_total - n_real, game.graph().num_nodes(), game.graph().num_edges());

    // AIs: SimpleAI for real players, PassiveAI for neutral
    std::vector<std::unique_ptr<PlayerInterface>> ais;
    for (int i = 0; i < n_real; i++) {
        ais.push_back(std::make_unique<SimpleAI>());
    }
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassiveAI>());
    }

    // RayLib init
    InitWindow(screen_w, screen_h, "CRisky — Spectator");
    SetTargetFPS(60);

    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    Renderer renderer(screen_w, screen_h);

    // Tick timing
    float game_speed = 1.0f;  // ticks per frame at 60 FPS
    float dt = 1.0f;          // one discrete tick
    bool paused = false;
    int tick_count = 0;

    std::vector<PlayerCommands> commands(n_total);

    while (!WindowShouldClose()) {
        camera.update();

        // Speed adjustment: +/- keys
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
            game_speed = std::min(game_speed * 2.0f, 64.0f);
        }
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
            game_speed = std::max(game_speed * 0.5f, 0.25f);
        }
        if (IsKeyPressed(KEY_SPACE)) {
            paused = !paused;
        }

        // One tick per frame
        if (!paused && !game.is_game_over()) {
            float frame_dt = dt * game_speed;
            for (int p = 0; p < n_total; p++) {
                commands[p] = PlayerCommands{};
                if (game.is_alive(p)) {
                    ais[p]->decide(game, p, commands[p]);
                }
            }
            game.tick(frame_dt, commands);
            tick_count++;
        }

        // Draw
        BeginDrawing();
        ClearBackground(Color{80, 120, 60, 255});  // grassy green

        renderer.draw(game, camera);

        // HUD
        char hud[128];
        snprintf(hud, sizeof(hud), "Tick: %d  Speed: %.2fx  FPS: %d%s",
                 tick_count, game_speed, GetFPS(), paused ? "  [PAUSED]" : "");
        DrawText(hud, 10, 10, 16, WHITE);

        if (game.is_game_over()) {
            DrawText("GAME OVER", screen_w / 2 - 60, screen_h / 2, 24, WHITE);
        }

        // Controls help
        DrawText("+/-: speed  Space: pause  WASD: pan  Q/E: rotate  Scroll: zoom  MMB: drag",
                 10, screen_h - 20, 10, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
