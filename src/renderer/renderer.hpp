#ifndef CRISKY_RENDERER_HPP
#define CRISKY_RENDERER_HPP

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/camera.hpp"

struct RenderConstants {
    float node_radius = 1.05f;
    float troop_dot_min_radius = 0.3f;
    float troop_dot_max_radius = 0.75f;
    float edge_outer_thickness = 0.75f;
    float edge_inner_thickness = 0.225f;
    float font_size_troop = 4.8f;
    float font_size_node_label = 5.6f;
    float troop_lane_offset_px = 0.45f;
    Color edge_outer_color = Color{60, 55, 50, 140};    // dark grey-brown, transparent
    Color edge_inner_color = Color{150, 140, 130, 255};  // lighter grey, opaque
    Color unowned_color = Color{210, 210, 200, 255};     // bright warm white-grey
};

class Renderer {
public:
    Renderer(int screen_w, int screen_h);

    void draw(const Game& game, const Camera2D_Custom& camera);

private:
    int screen_w_;
    int screen_h_;
    RenderConstants rc_;

    static constexpr int MAX_PLAYERS = 8;
    Color player_colors_[MAX_PLAYERS];

    void draw_edges(const Game& game, const Camera2D_Custom& camera, float scale);
    void draw_nodes(const Game& game, const Camera2D_Custom& camera, float scale);
    void draw_troop_groups(const Game& game, const Camera2D_Custom& camera, float scale);

    Color color_for_player(int owner) const;
    void draw_state_icon(NodeState state, Vector2 center, float radius) const;

    float base_zoom_ = 1.0f;  // set on first draw to make sizes relative
};

#endif
