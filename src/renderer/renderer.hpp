#ifndef CRISKY_RENDERER_HPP
#define CRISKY_RENDERER_HPP

#include "raylib.h"
#include "engine/game.hpp"
#include "renderer/camera.hpp"

struct RenderConstants {
    float node_radius = 14.0f;
    float troop_dot_min_radius = 4.0f;
    float troop_dot_max_radius = 10.0f;
    float edge_thickness = 1.0f;
    int font_size_troop = 12;
    int font_size_node_label = 14;
    float troop_lane_offset_px = 6.0f;  // perpendicular offset in screen pixels
    Color edge_color = LIGHTGRAY;
    Color unowned_color = DARKGREEN;
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

    void draw_edges(const Game& game, const Camera2D_Custom& camera);
    void draw_nodes(const Game& game, const Camera2D_Custom& camera);
    void draw_troop_groups(const Game& game, const Camera2D_Custom& camera);

    Color color_for_player(int owner) const;
    const char* state_letter(NodeState state) const;
};

#endif
