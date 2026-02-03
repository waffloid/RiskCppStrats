#ifndef CRISKY_RENDERER_HPP
#define CRISKY_RENDERER_HPP

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/camera.hpp"
#include "renderer/color_scheme.hpp"
#include <set>

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
    void draw_state_icon(NodeState state, Vector2 center, float radius) const;
    void draw_outlined_text(const char* text, int x, int y, int font_size, Color fg) const;

    float base_zoom_ = 1.0f;
};

#endif
