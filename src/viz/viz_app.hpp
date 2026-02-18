#ifndef CRISKY_VIZ_APP_HPP
#define CRISKY_VIZ_APP_HPP

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/renderer.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include <string>
#include <set>

// Shared rendering shell for game visualization.
// Encapsulates window, camera, renderer, theme management, background texture,
// keyboard shortcuts, frame lifecycle, and tick accumulation.
//
// Usage:
//   VizApp app("Title", 1400, 800);
//   app.init_camera(game.graph());
//   while (!app.should_close()) {
//       float dt; while ((dt = app.consume_tick()) > 0) { game.tick(dt, cmds); }
//       app.begin_frame();
//       app.draw_game(game);
//       app.begin_imgui();
//       host.draw();
//       app.end_imgui();
//       app.end_frame();
//   }
class VizApp {
public:
    VizApp(const std::string& title, int w = 1400, int h = 800,
           int scheme_idx = SCHEME_DEFAULT);
    ~VizApp();

    void init_camera(const Graph& graph);

    // Frame lifecycle
    bool should_close() const;
    void begin_frame();   // camera.update, keyboard, BeginDrawing, ClearBg, draw_background
    void draw_game(const Game& game, const std::set<int>* selected_nodes = nullptr);
    void begin_imgui();   // rlImGuiBegin + DockSpace (if show_debug)
    void end_imgui();     // rlImGuiEnd
    void end_frame();     // scheme notify, scanlines, EndDrawing

    // Tick accumulation — returns dt for this tick (0 = no tick).
    // Speed < 1: one tick per frame with dt = speed (smooth slow-motion).
    // Speed >= 1: multiple dt=1.0 ticks per frame (fast-forward).
    float consume_tick();

    // Accessors (references for PlaybackControls binding)
    float& speed() { return game_speed_; }
    bool& paused() { return paused_; }
    bool& show_debug() { return show_debug_; }
    Camera2D_Custom& camera() { return camera_; }
    Renderer& renderer() { return renderer_; }
    int screen_w() const;
    int screen_h() const;
    int scheme_idx() const { return scheme_idx_; }

private:
    Camera2D_Custom camera_;
    Renderer renderer_;
    Texture2D bg_tex_{};
    int scheme_idx_;

    float game_speed_ = 1.0f;
    bool paused_ = true;
    bool show_debug_ = true;
    float tick_accumulator_ = 0.0f;
    bool frame_accumulated_ = false;  // ensures game_speed_ added once per frame
    float scheme_notify_timer_ = 0.0f;

    void regenerate_bg_texture();
    void handle_keyboard();
};

#endif
