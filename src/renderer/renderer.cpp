#include "renderer/renderer.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

Renderer::Renderer(int screen_w, int screen_h)
    : screen_w_(screen_w), screen_h_(screen_h)
{
    player_colors_[0] = RED;
    player_colors_[1] = BLUE;
    player_colors_[2] = DARKGREEN;  // neutral player
    player_colors_[3] = YELLOW;
    player_colors_[4] = PURPLE;
    player_colors_[5] = ORANGE;
    player_colors_[6] = PINK;
    player_colors_[7] = SKYBLUE;
}

void Renderer::draw(const Game& game, const Camera2D_Custom& camera) {
    draw_edges(game, camera);
    draw_troop_groups(game, camera);
    draw_nodes(game, camera);
}

Color Renderer::color_for_player(int owner) const {
    if (owner < 0 || owner >= MAX_PLAYERS) return rc_.unowned_color;
    return player_colors_[owner];
}

const char* Renderer::state_letter(NodeState state) const {
    switch (state) {
        case NodeState::CAPITAL:    return "C";
        case NodeState::FACTORY:    return "F";
        case NodeState::POWERPLANT: return "P";
        case NodeState::FORT:       return "X";
        case NodeState::ARTILLERY:  return "A";
        default:                    return "";
    }
}

void Renderer::draw_edges(const Game& game, const Camera2D_Custom& camera) {
    const auto& graph = game.graph();
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx(sa, sb, rc_.edge_thickness, rc_.edge_color);
    }
}

void Renderer::draw_nodes(const Game& game, const Camera2D_Custom& camera) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    float r = rc_.node_radius;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        const auto& nd = nodes_data[i];
        Vector2 sp = camera.world_to_screen({node.x, node.y});

        // Filled circle colored by owner
        Color fill = color_for_player(nd.owner);
        DrawCircleV(sp, r, fill);
        DrawCircleLinesV(sp, r, BLACK);

        // State letter inside
        const char* letter = state_letter(nd.state);
        if (letter[0] != '\0') {
            int tw = MeasureText(letter, rc_.font_size_node_label);
            DrawText(letter,
                     static_cast<int>(sp.x) - tw / 2,
                     static_cast<int>(sp.y) - rc_.font_size_node_label / 2,
                     rc_.font_size_node_label, WHITE);
        }

        // Total troop count below the node
        int total = 0;
        for (int t : nd.troops) total += t;
        if (total > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", total);
            int tw = MeasureText(buf, rc_.font_size_troop);
            DrawText(buf,
                     static_cast<int>(sp.x) - tw / 2,
                     static_cast<int>(sp.y + r + 2),
                     rc_.font_size_troop, WHITE);
        }
    }
}

void Renderer::draw_troop_groups(const Game& game, const Camera2D_Custom& camera) {
    const auto& graph = game.graph();
    const auto& all_el = game.edge_lanes();

    for (const auto& el : all_el) {
        const auto& na = graph.nodes[el.node_a];
        const auto& nb = graph.nodes[el.node_b];
        Vector2 wa = {na.x, na.y};
        Vector2 wb = {nb.x, nb.y};

        for (int lane_idx = 0; lane_idx < 2; lane_idx++) {
            const auto& lane = el.lanes[lane_idx];
            for (const auto& group : lane.groups) {
                float t = group.position;
                Vector2 origin, dest;
                if (lane_idx == 0) {
                    origin = wa; dest = wb;
                } else {
                    origin = wb; dest = wa;
                }
                Vector2 world_pos = {
                    origin.x + t * (dest.x - origin.x),
                    origin.y + t * (dest.y - origin.y)
                };

                Vector2 sp = camera.world_to_screen(world_pos);

                // Dot radius based on count
                float frac = std::log2f(static_cast<float>(group.count + 1)) / 12.0f;
                frac = std::clamp(frac, 0.0f, 1.0f);
                float dot_r = rc_.troop_dot_min_radius + frac * (rc_.troop_dot_max_radius - rc_.troop_dot_min_radius);

                Color c = color_for_player(group.owner);
                if (group.retreating) {
                    c.a = 150;
                }
                DrawCircleV(sp, dot_r, c);

                // Troop count text
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", group.count);
                int tw = MeasureText(buf, rc_.font_size_troop - 2);
                DrawText(buf,
                         static_cast<int>(sp.x) - tw / 2,
                         static_cast<int>(sp.y) - (rc_.font_size_troop - 2) / 2,
                         rc_.font_size_troop - 2, WHITE);
            }
        }
    }
}
