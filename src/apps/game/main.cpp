#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <string_view>
#include <vector>

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"
#include "engine/game.hpp"
#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ui/human_player.hpp"
#include "ai/models.hpp"
#include "engine/graph_builder.hpp"

#include "ai/players/passive_player.hpp"

// QUBO model registration (defined in crisky_graph_algo)
extern void register_v5_qubo();
extern void register_v7();
extern void register_v8();
extern void register_v9();
extern void register_v10();
extern void register_v11();

#include "observability/metrics_collector.hpp"
#include "viz/imgui_theme.hpp"
#include "viz/panel_host.hpp"
#include "viz/ring_buffer.hpp"
#include "viz/panels/time_series_chart.hpp"
#include "viz/panels/bar_chart.hpp"
#include "viz/panels/graph_heatmap.hpp"
#include "viz/panels/stats_table.hpp"
#include "viz/panels/playback_controls.hpp"
#include "viz/overlays/node_heatmap.hpp"
#include "viz/overlays/gradient_arrows.hpp"

static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

static const char* parse_record_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--record=")) {
            return argv[i] + 9;
        }
        if (arg == "--record") {
            return "gameplay.mp4";
        }
    }
    return nullptr;
}

static float parse_record_delay(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--record-delay=")) {
            return std::strtof(argv[i] + 15, nullptr);
        }
    }
    return 0.0f;
}

static int parse_players_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--players=")) {
            int val = 0;
            auto sub = arg.substr(10);
            std::from_chars(sub.data(), sub.data() + sub.size(), val);
            if (val >= 2) return val;
            printf("--players must be >= 2, using default\n");
        }
    }
    return 3; // default
}

static bool parse_human_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string_view(argv[i]) == "--human") return true;
    }
    return false;
}

static std::vector<std::string> parse_model_args(int argc, char* argv[]) {
    std::vector<std::string> models;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--model=")) {
            models.emplace_back(arg.substr(8));
        }
    }
    // Validate
    for (const std::string& name : models) {
        if (!get_model(name)) {
            printf("Unknown model '%s'. Available:", name.c_str());
            for (const std::string& m : list_models()) printf(" %s", m.c_str());
            printf("\n");
            models.clear();
            return models;
        }
    }
    return models;
}

static int parse_scheme_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--scheme=")) {
            std::string_view name = arg.substr(9);
            for (int s = 0; s < NUM_COLOR_SCHEMES; s++) {
                if (iequals(name, COLOR_SCHEMES[s].name)) return s;
            }
            // Try numeric
            int val = 0;
            auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), val);
            if (ec == std::errc{} && val >= 0 && val < NUM_COLOR_SCHEMES) return val;
            printf("Unknown scheme '%.*s'. Available:", static_cast<int>(name.size()), name.data());
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
    register_v5_qubo();
    register_v7();
    register_v8();
    register_v9();
    register_v10();
    register_v11();

    uint64_t seed = 42;
    if (argc > 1 && argv[1][0] != '-') seed = static_cast<uint64_t>(std::atoll(argv[1]));

    int scheme_idx = parse_scheme_arg(argc, argv);
    const char* record_file = parse_record_arg(argc, argv);
    float record_delay = parse_record_delay(argc, argv);
    int num_players = parse_players_arg(argc, argv);
    bool human_mode = parse_human_arg(argc, argv);
    std::vector<std::string> model_args = parse_model_args(argc, argv);

    // Check for --scenario= flag
    std::string_view scenario_name;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--scenario=")) {
            scenario_name = arg.substr(11);
        }
    }

    int screen_w = 1280;
    int screen_h = 800;

    // Game setup — either a named scenario or the default Poisson map
    GameConfig config;
    std::unique_ptr<Game> game_ptr;
    int n_real;

    if (scenario_name == "buildup") {
        // test_knapsack_buildup_then_attack scenario
        std::vector<std::pair<float, float>> positions = {
            {0, 0}, {40, 0}, {80, 0}, {120, 0}, {160, 0}, {80, 30}
        };
        std::vector<std::pair<int, int>> edges = {
            {0, 1}, {1, 2}, {2, 3}, {3, 4}, {2, 5}
        };
        config.init_troop_count = 20;
        n_real = 2;
        num_players = 2;
        game_ptr = std::make_unique<Game>(config, build_graph(positions, edges), std::vector<int>{0, 4});
        game_ptr->set_node_state(1, NodeState::FACTORY, 0, 10);
        game_ptr->set_node_state(2, NodeState::POWERPLANT, 0, 10);
        game_ptr->set_node_state(5, NodeState::FACTORY, 0, 10);
        game_ptr->set_node_state(3, NodeState::DEFAULT, 1, 100);
        if (model_args.empty()) {
            if (human_mode)
                model_args = {"", "v1_knapsack"};
            else
                model_args = {"v1_knapsack_hybrid", "v0_expansion"};
        }
        printf("Scenario: buildup (you vs knapsack on linear graph)\n");
    } else if (scenario_name == "bipartite") {
        // K_{3,5} frontier attack scenario
        config.init_troop_count = 200;
        config.init_default_troops = 50;
        n_real = 2;
        num_players = 2;
        game_ptr = std::make_unique<Game>(config, build_bipartite(3, 5), std::vector<int>{0, 3});
        game_ptr->set_node_state(1, NodeState::DEFAULT, 0, 150);
        game_ptr->set_node_state(2, NodeState::DEFAULT, 0, 150);
        if (model_args.empty()) {
            if (human_mode)
                model_args = {"", "v1_knapsack"};
            else
                model_args = {"v0_expansion", "v1_knapsack"};
        }
        printf("Scenario: bipartite K_{3,5}\n");
    } else {
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

        Graph test_graph = Graph::generate_poisson(config, seed);
        if (test_graph.num_nodes() < 2) {
            printf("Graph too small (%d nodes), try different seed\n", test_graph.num_nodes());
            return 1;
        }

        auto capitals = test_graph.pick_spaced_capitals(num_players);
        n_real = num_players;

        game_ptr = std::make_unique<Game>(config, capitals, seed);
    }

    Game& game = *game_ptr;
    int n_total = game.n_players(); // includes neutral if present
    printf("Game: %d real players (+%d neutral), %d nodes, %d edges%s\n",
           n_real, n_total - n_real, game.graph().num_nodes(), game.graph().num_edges(),
           human_mode ? " [human playing]" : " [spectator]");

    // Player setup: slot per player, nullptr = human-controlled
    std::vector<std::unique_ptr<PlayerInterface>> ais;
    std::unique_ptr<HumanPlayer> human_player;
    if (human_mode) human_player = std::make_unique<HumanPlayer>();
    for (int i = 0; i < n_real; i++) {
        if (i == 0 && human_mode) {
            ais.push_back(nullptr); // human slot
        } else if (i < static_cast<int>(model_args.size())) {
            const auto* factory = get_model(model_args[i]);
            ais.push_back((*factory)(i));
            printf("P%d: %s\n", i, model_args[i].c_str());
        } else {
            ais.push_back(std::make_unique<DistributionAIPlayer>(i));
            printf("P%d: default (v0_expansion)\n", i);
        }
    }
    for (int i = n_real; i < n_total; i++) {
        ais.push_back(std::make_unique<PassivePlayer>());
    }

    // RayLib init
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screen_w, screen_h, "RiskC++ Strats");
    SetTargetFPS(60);

    // ImGui init
    rlImGuiSetup(true);
    ImPlot::CreateContext();
    ImGuiIO& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool show_debug = false;  // toggle with F1 key

    // Enable AI metrics for all DistributionAIPlayers; track one for viz
    std::vector<std::pair<int, DistributionAIPlayer*>> ai_players; // (player_id, ptr)
    for (int i = 0; i < n_real; i++) {
        if (auto* dp = dynamic_cast<DistributionAIPlayer*>(ais[i].get())) {
            dp->set_metrics_enabled(true);
            ai_players.push_back({i, dp});
        }
    }
    int tracked_ai_idx = 0; // index into ai_players
    DistributionAIPlayer* tracked_ai = ai_players.empty() ? nullptr : ai_players[0].second;
    int tracked_player_id = ai_players.empty() ? -1 : ai_players[0].first;

    // MetricsCollector for economy/combat/territory stats
    MetricsCollector metrics(n_real);

    // Tick counter (declared early so panel lambdas can capture it)
    int tick_count = 0;

    // Ring buffers for per-player time-series data
    std::vector<RingBuffer<float>> buf_troops(n_real, RingBuffer<float>(4096));
    std::vector<RingBuffer<float>> buf_nodes(n_real, RingBuffer<float>(4096));
    std::vector<RingBuffer<float>> buf_production(n_real, RingBuffer<float>(4096));
    std::vector<RingBuffer<float>> buf_factories(n_real, RingBuffer<float>(4096));
    std::vector<RingBuffer<float>> buf_kd_ratio(n_real, RingBuffer<float>(4096));

    // Panel host
    PanelHost panel_host;

    // Per-player time-series charts (standalone windows — dockable independently)
    std::vector<TimeSeriesChart*> player_charts;
    {
        const auto& scheme = COLOR_SCHEMES[scheme_idx];
        auto make_chart = [&](const char* name, const char* y_label,
                              std::vector<RingBuffer<float>>& bufs) {
            auto chart = std::make_unique<TimeSeriesChart>(name, "Tick", y_label);
            for (int p = 0; p < std::min(n_real, 4); p++) {
                unsigned int c = IM_COL32(scheme.player_colors[p].r, scheme.player_colors[p].g,
                                          scheme.player_colors[p].b, scheme.player_colors[p].a);
                char label[16];
                std::snprintf(label, sizeof(label), "P%d", p);
                chart->add_series(label, c, &bufs[p]);
            }
            player_charts.push_back(chart.get());
            return chart;
        };
        panel_host.add(make_chart("Territory", "Nodes", buf_nodes));
        panel_host.add(make_chart("Troops", "Count", buf_troops));
        panel_host.add(make_chart("Production", "Troops/tick", buf_production));
        panel_host.add(make_chart("Factories", "Count", buf_factories));
        panel_host.add(make_chart("K/D Ratio", "Ratio", buf_kd_ratio));
    }

    // Heatmaps and AI decision panels (all dockable)
    if (tracked_ai) {
        panel_host.add(std::make_unique<GraphHeatmap>(
            "P0 Distribution (troop target %)", &game.graph(),
            [&]() -> std::vector<float> {
                return tracked_ai->decision_snapshot().smoothed;
            }
        ));
        panel_host.add(std::make_unique<GraphHeatmap>(
            "P0 Potential (deficit signal)", &game.graph(),
            [&]() -> std::vector<float> {
                return tracked_ai->decision_snapshot().gradient;
            }
        ));
        panel_host.add(std::make_unique<GraphHeatmap>(
            "P0 Combined (pre-softmax)", &game.graph(),
            [&]() -> std::vector<float> {
                return tracked_ai->decision_snapshot().combined_scores;
            }
        ));

        // Per sub-agent raw score heatmaps
        for (int sa_idx = 0; sa_idx < 3; sa_idx++) {
            const char* names[] = {"P0 Economy Scores", "P0 Expansion Scores", "P0 War Scores"};
            panel_host.add(std::make_unique<GraphHeatmap>(
                names[sa_idx], &game.graph(),
                [&, sa_idx]() -> std::vector<float> {
                    auto& sa = tracked_ai->decision_snapshot().sub_agents;
                    if (sa_idx < static_cast<int>(sa.size()))
                        return sa[sa_idx].raw_scores;
                    return {};
                }
            ));
        }

        // Sub-agent weight bar chart
        panel_host.add(std::make_unique<BarChart>(
            "Sub-agent Weights", "Weight",
            [&]() {
                std::vector<std::pair<std::string, float>> bars;
                for (auto& sa : tracked_ai->decision_snapshot().sub_agents)
                    bars.push_back({sa.name, sa.weight});
                return bars;
            }
        ));

        // Per-node troop distribution bar chart (edge-interpolated)
        panel_host.add(std::make_unique<BarChart>(
            "P0 Troop Distribution", "Troops",
            [&]() {
                std::vector<std::pair<std::string, float>> bars;
                auto t = game.effective_troops(tracked_player_id);
                for (int i = 0; i < static_cast<int>(t.size()); i++) {
                    if (game.node_data()[i].owner == tracked_player_id)
                        bars.push_back({std::to_string(i), t[i]});
                }
                return bars;
            }
        ));
    }

    // Node index gradient (verifies spatial reordering: should sweep smoothly)
    panel_host.add(std::make_unique<GraphHeatmap>(
        "Node Index Gradient", &game.graph(),
        [&]() -> std::vector<float> {
            int n = game.graph().num_nodes();
            std::vector<float> v(n);
            float denom = (n > 1) ? static_cast<float>(n - 1) : 1.0f;
            for (int i = 0; i < n; i++) v[i] = static_cast<float>(i) / denom;
            return v;
        }
    ));

    // Stats table
    panel_host.add(std::make_unique<StatsTable>("Game Stats", [&]() {
        std::vector<std::pair<std::string, std::string>> rows;
        auto fmt = [](const char* f, auto v) {
            char b[64]; std::snprintf(b, sizeof(b), f, v); return std::string(b);
        };
        rows.push_back({"Tick", fmt("%d", tick_count)});
        rows.push_back({"Nodes", fmt("%d", game.graph().num_nodes())});
        for (int p = 0; p < n_real; p++) {
            int nodes = 0, troops = 0;
            for (int i = 0; i < game.graph().num_nodes(); i++) {
                if (game.node_data()[i].owner == p) nodes++;
                if (p < static_cast<int>(game.node_data()[i].troops.size()))
                    troops += game.node_data()[i].troops[p];
            }
            rows.push_back({fmt("P%d Nodes", p), fmt("%d", nodes)});
            rows.push_back({fmt("P%d Troops", p), fmt("%d", troops)});
        }
        return rows;
    }));

    // Overlays (toggled with H/G keys)
    std::unique_ptr<NodeHeatmapOverlay> heatmap_overlay;
    std::unique_ptr<GradientArrowsOverlay> gradient_overlay;
    if (tracked_ai) {
        heatmap_overlay = std::make_unique<NodeHeatmapOverlay>(
            "Distribution Heatmap", &game.graph(),
            [&]() -> std::vector<float> {
                return tracked_ai->decision_snapshot().smoothed;
            }
        );
        heatmap_overlay->visible = false;

        gradient_overlay = std::make_unique<GradientArrowsOverlay>(
            "Gradient Arrows", &game.graph(),
            [&]() -> std::vector<float> {
                return tracked_ai->decision_snapshot().gradient;
            }
        );
        gradient_overlay->visible = false;
    }

    // Generate tileable noise background
    const int bg_tile = 256;
    Texture2D bg_tex = {0};
    regenerate_bg_texture(bg_tex, COLOR_SCHEMES[scheme_idx].background, bg_tile);

    Camera2D_Custom camera;
    camera.fit_to_graph(game.graph(), screen_w, screen_h);

    Renderer renderer(screen_w, screen_h, &COLOR_SCHEMES[scheme_idx]);

    // Recording: pipe raw RGBA frames to ffmpeg (opened after delay elapses)
    FILE* ffmpeg_pipe = nullptr;
    int record_w = 0, record_h = 0;
    float record_elapsed = 0.0f;
    bool record_started = false;

    // Tick timing
    float game_speed = 1.0f;  // ticks per frame at 60 FPS
    float dt = 0.25f;         // one discrete tick
    bool paused = false;
    int game_over_frames = 0; // count frames after game over for auto-exit

    // Scheme switch notification
    float scheme_notify_timer = 0.0f;

    std::vector<PlayerCommands> commands(n_total);

    while (!WindowShouldClose()) {
        screen_w = GetScreenWidth();
        screen_h = GetScreenHeight();
        camera.update(imgui_io.WantCaptureMouse, imgui_io.WantCaptureKeyboard);

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

        // Color scheme switching: [ and ] to cycle
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
        if (IsKeyPressed(KEY_Z)) {
            renderer.set_zen_mode(!renderer.zen_mode());
        }
        if (IsKeyPressed(KEY_F1)) {
            show_debug = !show_debug;
        }
        if (IsKeyPressed(KEY_H) && heatmap_overlay) {
            heatmap_overlay->visible = !heatmap_overlay->visible;
        }
        if (IsKeyPressed(KEY_G) && gradient_overlay) {
            gradient_overlay->visible = !gradient_overlay->visible;
        }
        // TAB: cycle which AI player the debug panels inspect
        if (IsKeyPressed(KEY_TAB) && ai_players.size() > 1) {
            tracked_ai_idx = (tracked_ai_idx + 1) % static_cast<int>(ai_players.size());
            tracked_ai = ai_players[tracked_ai_idx].second;
            tracked_player_id = ai_players[tracked_ai_idx].first;
        }

        if (scheme_notify_timer > 0.0f) {
            scheme_notify_timer -= GetFrameTime();
        }

        // Process human player input (every frame, not just on ticks)
        if (human_player) {
            human_player->process_input(camera, screen_w, screen_h, game, 0);
        }

        // One tick per frame
        if (!paused && !game.is_game_over()) {
            float frame_dt = dt * game_speed;
            for (int p = 0; p < n_total; p++) {
                commands[p] = PlayerCommands{};
                if (game.is_alive(p)) {
                    if (p == 0 && human_player) {
                        human_player->decide(game, p, commands[p]);
                    } else {
                        ais[p]->decide(game, p, commands[p]);
                    }
                }
            }
            game.tick(frame_dt, commands);
            tick_count++;

            // Update ring buffers for debug viz
            if (show_debug) {
                metrics.collect(game, tick_count);
                for (int p = 0; p < n_real; p++) {
                    const auto& s = metrics.snapshot(p);
                    buf_nodes[p].push(static_cast<float>(s.nodes_owned));
                    buf_troops[p].push(static_cast<float>(s.total_troops));
                    buf_production[p].push(static_cast<float>(s.production_per_tick));
                    buf_factories[p].push(static_cast<float>(s.factories_owned));
                    buf_kd_ratio[p].push(s.kd_ratio);
                }
            }
        }

        // Draw
        BeginDrawing();
        ClearBackground(COLOR_SCHEMES[scheme_idx].background);

        // Draw background (gradient for Retrowave, tiled noise for others)
        renderer.draw_background(screen_w, screen_h, camera, bg_tex);

        // Pass selection to renderer so halos draw below edges
        const std::set<int>* sel = human_player ? &human_player->selected_nodes() : nullptr;
        renderer.draw(game, camera, sel);

        // Human player overlay UI (drag circle)
        if (human_player) {
            human_player->render(camera, screen_w, screen_h, game, 0, COLOR_SCHEMES[scheme_idx]);
        }

        // AI debug overlays (world-space, need BeginMode2D)
        if (show_debug && (heatmap_overlay || gradient_overlay)) {
            BeginMode2D(Camera2D{
                .offset = camera.offset(),
                .target = {0, 0},
                .rotation = camera.rotation(),
                .zoom = camera.zoom()
            });
            if (heatmap_overlay) heatmap_overlay->draw();
            if (gradient_overlay) gradient_overlay->draw();
            EndMode2D();
        }

        // Scanline overlay (Terminal theme)
        renderer.draw_scanlines(screen_w, screen_h);

        // HUD
        char hud[160];
        snprintf(hud, sizeof(hud), "Tick: %d  Speed: %.2fx  FPS: %d%s%s  [%s]",
                 tick_count, game_speed, GetFPS(), paused ? "  [PAUSED]" : "",
                 renderer.zen_mode() ? "  [ZEN]" : "",
                 COLOR_SCHEMES[scheme_idx].name);
        DrawText(hud, 10, 10, 16, WHITE);
        if (show_debug && tracked_ai) {
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "Inspecting: P%d  [TAB to cycle]", tracked_player_id);
            DrawText(dbg, 10, 28, 14, LIGHTGRAY);
        }

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
        DrawText("CLICK: select  SHIFT+CLICK: add/toggle  ALT+CLICK: deselect  DRAG: circle select  SHIFT/ALT+DRAG: add/remove",
                 10, screen_h - 30, 10, DARKGRAY);
        DrawText("QERF: send troops  1-4: build  +/-: speed  Space: pause  WASD: pan  I/O: zoom  K/L: rotate  [/]: themes  Z: zen  F1: debug  H: heatmap  G: gradient  TAB: cycle AI",
                 10, screen_h - 15, 10, DARKGRAY);

        // ImGui debug panels
        apply_imgui_theme(COLOR_SCHEMES[scheme_idx]);
        rlImGuiBegin();
        if (show_debug) {
            // Sync chart colors with current theme
            {
                const auto& s = COLOR_SCHEMES[scheme_idx];
                for (auto* chart : player_charts) {
                    for (int p = 0; p < std::min(n_real, 4); p++) {
                        chart->set_series_color(p, IM_COL32(s.player_colors[p].r,
                            s.player_colors[p].g, s.player_colors[p].b, s.player_colors[p].a));
                    }
                }
            }
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                         ImGuiDockNodeFlags_PassthruCentralNode);
            panel_host.draw();
        }
        rlImGuiEnd();

        EndDrawing();

        // Start recording after delay
        if (record_file && !record_started) {
            record_elapsed += GetFrameTime();
            if (record_elapsed >= record_delay) {
                record_started = true;
                record_w = GetRenderWidth();
                record_h = GetRenderHeight();
                char cmd[512];
                snprintf(cmd, sizeof(cmd),
                    "ffmpeg -y -f rawvideo -pix_fmt rgba -s %dx%d -r 60 "
                    "-i - -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p \"%s\"",
                    record_w, record_h, record_file);
                ffmpeg_pipe = popen(cmd, "w");
                if (!ffmpeg_pipe) {
                    printf("Failed to open ffmpeg pipe for recording\n");
                    record_file = nullptr;
                } else {
                    printf("Recording to %s (%dx%d @ 60fps, started after %.1fs delay)\n",
                           record_file, record_w, record_h, record_delay);
                }
            }
        }

        // Record frame
        if (ffmpeg_pipe) {
            Image screen_img = LoadImageFromScreen();
            fwrite(screen_img.data, 1, record_w * record_h * 4, ffmpeg_pipe);
            UnloadImage(screen_img);
        }

        // Auto-exit in record mode after game over
        if (record_file && game.is_game_over()) {
            game_over_frames++;
            if (game_over_frames > 180) break; // 3 seconds at 60fps
        }
    }

    if (ffmpeg_pipe) {
        pclose(ffmpeg_pipe);
        printf("Recording saved to %s\n", record_file);
    }

    UnloadTexture(bg_tex);
    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
