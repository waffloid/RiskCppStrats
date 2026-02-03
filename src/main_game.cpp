#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <memory>

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include "player/attention_ai.hpp"

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

static int parse_scheme_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--scheme=", 9) == 0) {
            const char* name = argv[i] + 9;
            for (int s = 0; s < NUM_COLOR_SCHEMES; s++) {
                // Case-insensitive comparison
                const char* a = name;
                const char* b = COLOR_SCHEMES[s].name;
                bool match = true;
                while (*a && *b) {
                    if (tolower(*a) != tolower(*b)) { match = false; break; }
                    a++; b++;
                }
                if (match && *a == '\0' && *b == '\0') return s;
            }
            // Try numeric
            int val = atoi(name);
            if (val >= 0 && val < NUM_COLOR_SCHEMES) return val;
            printf("Unknown scheme '%s'. Available:", name);
            for (int s = 0; s < NUM_COLOR_SCHEMES; s++) {
                printf(" %s", COLOR_SCHEMES[s].name);
            }
            printf("\n");
        }
    }
    return SCHEME_DEFAULT;
}

static void regenerate_bg_texture(Texture2D& bg_tex, const Color& bg_color, int bg_tile) {
    Image bg_img = GenImageWhiteNoise(bg_tile, bg_tile, 0.5f);
    Color* pixels = LoadImageColors(bg_img);
    for (int i = 0; i < bg_tile * bg_tile; i++) {
        int noise = static_cast<int>(pixels[i].r) - 128;
        int offset = noise / 40;
        pixels[i].r = static_cast<unsigned char>(std::clamp(static_cast<int>(bg_color.r) + offset, 0, 255));
        pixels[i].g = static_cast<unsigned char>(std::clamp(static_cast<int>(bg_color.g) + offset, 0, 255));
        pixels[i].b = static_cast<unsigned char>(std::clamp(static_cast<int>(bg_color.b) + offset, 0, 255));
        pixels[i].a = 255;
    }
    UnloadImage(bg_img);
    bg_img = {pixels, bg_tile, bg_tile, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if (bg_tex.id > 0) UnloadTexture(bg_tex);
    bg_tex = LoadTextureFromImage(bg_img);
    SetTextureWrap(bg_tex, TEXTURE_WRAP_REPEAT);
    UnloadImageColors(pixels);
}

int main(int argc, char* argv[]) {
    uint64_t seed = 42;
    if (argc > 1 && argv[1][0] != '-') seed = static_cast<uint64_t>(std::atoll(argv[1]));

    int scheme_idx = parse_scheme_arg(argc, argv);

    int screen_w = 1280;
    int screen_h = 800;

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
        ais.push_back(std::make_unique<AttentionAI>(i));
    }
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassiveAI>());
    }

    // RayLib init
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screen_w, screen_h, "CRisky — Spectator");
    SetTargetFPS(60);

    // Generate tileable noise background
    const int bg_tile = 256;
    Texture2D bg_tex = {0};
    regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);

    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    Renderer renderer(screen_w, screen_h, &COLOR_SCHEMES[scheme_idx]);

    // Tick timing
    float game_speed = 1.0f;  // ticks per frame at 60 FPS
    float dt = 1.0f;          // one discrete tick
    bool paused = false;
    int tick_count = 0;

    // Scheme switch notification
    float scheme_notify_timer = 0.0f;

    std::vector<PlayerCommands> commands(n_total);

    while (!WindowShouldClose()) {
        screen_w = GetScreenWidth();
        screen_h = GetScreenHeight();
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

        // Color scheme switching: 1-9 for first 9, [ and ] to cycle all
        for (int k = 0; k < NUM_COLOR_SCHEMES && k < 9; k++) {
            if (IsKeyPressed(KEY_ONE + k)) {
                scheme_idx = k;
                renderer.set_scheme(&COLOR_SCHEMES[scheme_idx]);
                regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);
                scheme_notify_timer = 2.0f;
            }
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
            scheme_idx = (scheme_idx + 1) % NUM_COLOR_SCHEMES;
            renderer.set_scheme(&COLOR_SCHEMES[scheme_idx]);
            regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);
            scheme_notify_timer = 2.0f;
        }
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            scheme_idx = (scheme_idx - 1 + NUM_COLOR_SCHEMES) % NUM_COLOR_SCHEMES;
            renderer.set_scheme(&COLOR_SCHEMES[scheme_idx]);
            regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);
            scheme_notify_timer = 2.0f;
        }
        if (IsKeyPressed(KEY_ZERO)) {
            scheme_idx = 0;
            renderer.set_scheme(&COLOR_SCHEMES[scheme_idx]);
            regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);
            scheme_notify_timer = 2.0f;
        }

        if (scheme_notify_timer > 0.0f) {
            scheme_notify_timer -= GetFrameTime();
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
        ClearBackground(COLOR_SCHEMES[scheme_idx].background);

        // Draw background (gradient for Retrowave, tiled noise for others)
        renderer.draw_background(screen_w, screen_h, camera, bg_tex);

        renderer.draw(game, camera);

        // Scanline overlay (Terminal theme)
        renderer.draw_scanlines(screen_w, screen_h);

        // HUD
        char hud[128];
        snprintf(hud, sizeof(hud), "Tick: %d  Speed: %.2fx  FPS: %d%s  [%s]",
                 tick_count, game_speed, GetFPS(), paused ? "  [PAUSED]" : "",
                 COLOR_SCHEMES[scheme_idx].name);
        DrawText(hud, 10, 10, 16, WHITE);

        if (game.is_game_over()) {
            DrawText("GAME OVER", screen_w / 2 - 60, screen_h / 2, 24, WHITE);
        }

        // Scheme switch notification
        if (scheme_notify_timer > 0.0f) {
            char notify[64];
            snprintf(notify, sizeof(notify), "Theme: %s", COLOR_SCHEMES[scheme_idx].name);
            int tw = MeasureText(notify, 24);
            unsigned char alpha = static_cast<unsigned char>(
                std::min(1.0f, scheme_notify_timer) * 255);
            DrawText(notify, screen_w / 2 - tw / 2, 50, 24, Color{255, 255, 255, alpha});
        }

        // Controls help
        DrawText("+/-: speed  Space: pause  WASD: pan  Q/E: rotate  Scroll: zoom  [/]: cycle themes  0-9: select theme",
                 10, screen_h - 20, 10, DARKGRAY);

        EndDrawing();
    }

    UnloadTexture(bg_tex);
    CloseWindow();
    return 0;
}
