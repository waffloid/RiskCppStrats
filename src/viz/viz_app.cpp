#include "viz/viz_app.hpp"
#include "viz/imgui_theme.hpp"

#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

#include <algorithm>
#include <cstdio>

VizApp::VizApp(const std::string& title, int w, int h, int scheme_idx)
    : renderer_(w, h, &COLOR_SCHEMES[scheme_idx]), scheme_idx_(scheme_idx) {

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(w, h, title.c_str());
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    regenerate_bg_texture();
}

VizApp::~VizApp() {
    if (bg_tex_.id > 0) UnloadTexture(bg_tex_);
    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();
}

void VizApp::init_camera(const Graph& graph) {
    camera_.fit_to_graph(graph, GetScreenWidth(), GetScreenHeight());
}

bool VizApp::should_close() const {
    return WindowShouldClose();
}

void VizApp::begin_frame() {
    ImGuiIO& io = ImGui::GetIO();
    camera_.update(io.WantCaptureMouse, io.WantCaptureKeyboard);
    handle_keyboard();

    if (scheme_notify_timer_ > 0.0f) {
        scheme_notify_timer_ -= GetFrameTime();
    }

    BeginDrawing();
    ClearBackground(COLOR_SCHEMES[scheme_idx_].background);
    renderer_.draw_background(GetScreenWidth(), GetScreenHeight(), camera_, bg_tex_);
}

void VizApp::draw_game(const Game& game, const std::set<int>* selected_nodes) {
    renderer_.draw(game, camera_, selected_nodes);
}

void VizApp::begin_imgui() {
    apply_imgui_theme(COLOR_SCHEMES[scheme_idx_]);
    rlImGuiBegin();
    if (show_debug_) {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
    }
}

void VizApp::end_imgui() {
    rlImGuiEnd();
}

void VizApp::end_frame() {
    // Theme switch notification
    if (scheme_notify_timer_ > 0.0f) {
        char notify[64];
        std::snprintf(notify, sizeof(notify), "Theme: %s", COLOR_SCHEMES[scheme_idx_].name);
        int tw = MeasureText(notify, 24);
        unsigned char alpha = static_cast<unsigned char>(
            std::min(1.0f, scheme_notify_timer_) * 255);
        DrawText(notify, GetScreenWidth() / 2 - tw / 2, 50, 24,
                 Color{255, 255, 255, alpha});
    }

    renderer_.draw_scanlines(GetScreenWidth(), GetScreenHeight());
    EndDrawing();
}

float VizApp::consume_tick() {
    if (paused_) {
        tick_accumulator_ = 0.0f;
        frame_accumulated_ = false;
        return 0.0f;
    }
    if (!frame_accumulated_) {
        tick_accumulator_ += game_speed_;
        frame_accumulated_ = true;
    }
    if (game_speed_ < 1.0f) {
        // Slow-motion: one tick per frame with fractional dt
        if (tick_accumulator_ > 0.0f) {
            float dt = tick_accumulator_;
            tick_accumulator_ = 0.0f;
            return dt;
        }
        frame_accumulated_ = false;
        return 0.0f;
    }
    // Fast-forward: multiple dt=1.0 ticks per frame
    if (tick_accumulator_ >= 1.0f) {
        tick_accumulator_ -= 1.0f;
        return 1.0f;
    }
    frame_accumulated_ = false;
    return 0.0f;
}

int VizApp::screen_w() const { return GetScreenWidth(); }
int VizApp::screen_h() const { return GetScreenHeight(); }

void VizApp::handle_keyboard() {
    if (IsKeyPressed(KEY_SPACE)) {
        paused_ = !paused_;
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
        game_speed_ = std::min(game_speed_ * 2.0f, 64.0f);
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        game_speed_ = std::max(game_speed_ * 0.5f, 0.25f);
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
        scheme_idx_ = (scheme_idx_ + 1) % NUM_COLOR_SCHEMES;
        renderer_.set_scheme(&COLOR_SCHEMES[scheme_idx_]);
        regenerate_bg_texture();
        scheme_notify_timer_ = 2.0f;
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
        scheme_idx_ = (scheme_idx_ - 1 + NUM_COLOR_SCHEMES) % NUM_COLOR_SCHEMES;
        renderer_.set_scheme(&COLOR_SCHEMES[scheme_idx_]);
        regenerate_bg_texture();
        scheme_notify_timer_ = 2.0f;
    }
    if (IsKeyPressed(KEY_Z)) {
        renderer_.set_zen_mode(!renderer_.zen_mode());
    }
    if (IsKeyPressed(KEY_F1)) {
        show_debug_ = !show_debug_;
    }
}

void VizApp::regenerate_bg_texture() {
    constexpr int tile = 256;
    const Color& bg = COLOR_SCHEMES[scheme_idx_].background;

    Image img = GenImageWhiteNoise(tile, tile, 0.5f);
    Color* pixels = LoadImageColors(img);
    for (int i = 0; i < tile * tile; i++) {
        int noise = static_cast<int>(pixels[i].r) - 128;
        int offset = noise / 40;
        pixels[i].r = static_cast<unsigned char>(std::clamp(static_cast<int>(bg.r) + offset, 0, 255));
        pixels[i].g = static_cast<unsigned char>(std::clamp(static_cast<int>(bg.g) + offset, 0, 255));
        pixels[i].b = static_cast<unsigned char>(std::clamp(static_cast<int>(bg.b) + offset, 0, 255));
        pixels[i].a = 255;
    }
    UnloadImage(img);
    img = {pixels, tile, tile, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if (bg_tex_.id > 0) UnloadTexture(bg_tex_);
    bg_tex_ = LoadTextureFromImage(img);
    SetTextureWrap(bg_tex_, TEXTURE_WRAP_REPEAT);
    UnloadImageColors(pixels);
}
