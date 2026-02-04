#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include "player/attention_ai.hpp"
#include "player/passive_ai.hpp"

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

int main() {
    uint64_t seed = 42;
    int screen_w = 1280;
    int screen_h = 800;
    int ticks = 2000;
    float dt = 0.25f;

    // Game setup
    GameConfig config;
    config.poisson_intensity = 0.04f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 7;
    config.init_default_troops = 25;

    std::vector<int> capitals = {0, 1};
    Game game(config, capitals, seed);
    int n_total = game.n_players();

    printf("Running %d ticks with 2 AttentionAIs...\n", ticks);

    // Two AttentionAIs + passive for neutral
    std::vector<std::unique_ptr<PlayerInterface>> ais;
    ais.push_back(std::make_unique<AttentionAI>(0));
    ais.push_back(std::make_unique<AttentionAI>(1));
    for (int i = 2; i < n_total; i++) {
        ais.push_back(std::make_unique<PassiveAI>());
    }

    // Simulate
    std::vector<PlayerCommands> commands(n_total);
    for (int t = 0; t < ticks; t++) {
        if (game.is_game_over()) {
            printf("Game over at tick %d\n", t);
            break;
        }
        for (int p = 0; p < n_total; p++) {
            commands[p] = PlayerCommands{};
            if (game.is_alive(p)) {
                ais[p]->decide(game, p, commands[p]);
            }
        }
        game.tick(dt, commands);
    }
    printf("Simulation done. Taking screenshots...\n");

    // Init window
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screen_w, screen_h, "RiskC++ Strats — Theme Screenshots");

    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    const int bg_tile = 256;
    Texture2D bg_tex = {0};

    Renderer renderer(screen_w, screen_h);
    RenderTexture2D target = LoadRenderTexture(screen_w, screen_h);

    for (int s = 0; s < NUM_COLOR_SCHEMES; s++) {
        renderer.set_scheme(&COLOR_SCHEMES[s]);
        regenerate_bg_texture(bg_tex, COLOR_SCHEMES[s].background, bg_tile);

        BeginTextureMode(target);
        ClearBackground(COLOR_SCHEMES[s].background);
        renderer.draw_background(screen_w, screen_h, camera, bg_tex);
        renderer.draw(game, camera);
        renderer.draw_scanlines(screen_w, screen_h);

        // Theme name label
        char label[64];
        snprintf(label, sizeof(label), "%s", COLOR_SCHEMES[s].name);
        int tw = MeasureText(label, 24);
        DrawText(label, screen_w / 2 - tw / 2, 10, 24, WHITE);

        EndTextureMode();

        // Extract image from render texture (flip vertically — RenderTexture is upside down)
        Image img = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&img);
        char filename[128];
        snprintf(filename, sizeof(filename), "screenshots/theme_%02d_%s.png", s, COLOR_SCHEMES[s].name);
        ExportImage(img, filename);
        UnloadImage(img);
        printf("  Saved %s\n", filename);
    }

    UnloadRenderTexture(target);

    UnloadTexture(bg_tex);
    CloseWindow();

    printf("Done! %d screenshots saved to screenshots/\n", NUM_COLOR_SCHEMES);
    return 0;
}
