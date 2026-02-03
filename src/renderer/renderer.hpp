#ifndef CRISKY_RENDERER_HPP
#define CRISKY_RENDERER_HPP

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include <set>
#include <vector>

struct SmoothedColor {
    float r = -1.0f, g = -1.0f, b = -1.0f;

    Color update(Color target, float alpha) {
        float tr = target.r / 255.0f, tg = target.g / 255.0f, tb = target.b / 255.0f;
        if (r < 0.0f) { r = tr; g = tg; b = tb; }
        else { r += alpha * (tr - r); g += alpha * (tg - g); b += alpha * (tb - b); }
        return {
            static_cast<unsigned char>(r * 255.0f),
            static_cast<unsigned char>(g * 255.0f),
            static_cast<unsigned char>(b * 255.0f), 255
        };
    }
};

struct RenderConstants {
    float node_radius = 1.05f;
    float troop_dot_min_radius = 0.1f;
    float troop_dot_max_radius = 0.75f;
    float edge_outer_thickness = 0.75f;
    float edge_inner_thickness = 0.225f;
    float font_size_troop = 9.6f;
    float font_size_node_label = 11.2f;
    float troop_lane_offset_px = 0.45f;
    float shadow_offset_x = 1.0f;
    float shadow_offset_y = 2.5f;
};

class Renderer {
public:
    Renderer(int screen_w, int screen_h, const ColorScheme* scheme = &COLOR_SCHEMES[SCHEME_DEFAULT]);

    void draw(const Game& game, const Camera2D_Custom& camera,
             const std::set<int>* selected_nodes = nullptr);
    void draw_background(int screen_w, int screen_h, const Camera2D_Custom& camera, Texture2D noise_tex);
    void draw_scanlines(int screen_w, int screen_h);
    void set_scheme(const ColorScheme* scheme) { scheme_ = scheme; }
    const ColorScheme* scheme() const { return scheme_; }
    void set_zen_mode(bool on) { zen_mode_ = on; }
    bool zen_mode() const { return zen_mode_; }

private:
    int screen_w_;
    int screen_h_;
    RenderConstants rc_;
    const ColorScheme* scheme_;

    static constexpr int MAX_PLAYERS = 8;

    void draw_selection_halos(const Game& game, const Camera2D_Custom& camera,
                              float scale, const std::set<int>& selected_nodes);
    void draw_edges(const Game& game, const Camera2D_Custom& camera, float scale);
    void draw_node_circles(const Game& game, const Camera2D_Custom& camera, float scale);
    void draw_node_labels(const Game& game, const Camera2D_Custom& camera, float scale);
    void draw_troop_groups(const Game& game, const Camera2D_Custom& camera, float scale);

    Color color_for_player(int owner) const;
    float zen_sigmoid(int count, int num_nodes) const;
    Color zen_node_color(const int* troops, int n_players, int num_nodes) const;
    Color zen_edge_color(const int* hue_troops, int n_players,
                         int transit_total, int num_nodes) const;
    void draw_state_icon(NodeState state, Vector2 center, float radius) const;
    void draw_outlined_text(const char* text, int x, int y, int font_size, Color fg) const;

    float base_zoom_ = 1.0f;
    bool zen_mode_ = false;
    float zen_total_troops_ = 0.0f;

    // Per-element smoothed colors for zen mode
    std::vector<SmoothedColor> zen_edge_colors_;
    std::vector<SmoothedColor> zen_node_colors_;
};

#endif
