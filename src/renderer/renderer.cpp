#include "renderer/renderer.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

Renderer::Renderer(int screen_w, int screen_h)
    : screen_w_(screen_w), screen_h_(screen_h)
{
    // Bright unsaturated (pastel-ish but vivid)
    player_colors_[0] = Color{255, 120, 120, 255};  // bright red
    player_colors_[1] = Color{120, 160, 255, 255};  // bright blue
    player_colors_[2] = Color{180, 180, 180, 255};  // neutral grey
    player_colors_[3] = Color{255, 240, 100, 255};  // bright yellow
    player_colors_[4] = Color{200, 140, 255, 255};  // bright purple
    player_colors_[5] = Color{255, 180, 100, 255};  // bright orange
    player_colors_[6] = Color{255, 130, 200, 255};  // bright pink
    player_colors_[7] = Color{100, 230, 240, 255};  // bright cyan
}

void Renderer::draw(const Game& game, const Camera2D_Custom& camera) {
    if (base_zoom_ <= 0.0f) base_zoom_ = camera.zoom();
    float scale = camera.zoom() / base_zoom_;
    draw_edges(game, camera, scale);
    draw_troop_groups(game, camera, scale);
    draw_nodes(game, camera, scale);
}

Color Renderer::color_for_player(int owner) const {
    if (owner < 0 || owner >= MAX_PLAYERS) return rc_.unowned_color;
    return player_colors_[owner];
}

void Renderer::draw_state_icon(NodeState state, Vector2 center, float radius) const {
    // Desaturated white/grey icons drawn inside nodes
    Color icon_col = Color{255, 255, 255, 200};
    float s = radius * 0.55f;  // icon scale relative to node radius

    switch (state) {
        case NodeState::CAPITAL: {
            // Star shape (5-point)
            for (int i = 0; i < 5; i++) {
                float angle1 = -90.0f + i * 72.0f;
                float angle2 = -90.0f + (i + 2) * 72.0f;
                float r1 = angle1 * 3.14159265f / 180.0f;
                float r2 = angle2 * 3.14159265f / 180.0f;
                Vector2 p1 = {center.x + s * std::cos(r1), center.y + s * std::sin(r1)};
                Vector2 p2 = {center.x + s * std::cos(r2), center.y + s * std::sin(r2)};
                DrawLineEx(p1, p2, 1.5f, icon_col);
            }
            break;
        }
        case NodeState::FACTORY: {
            // Gear: inner circle + 4 tick marks
            float inner = s * 0.4f;
            DrawCircleLinesV(center, inner, icon_col);
            for (int i = 0; i < 4; i++) {
                float angle = i * 90.0f * 3.14159265f / 180.0f;
                Vector2 p1 = {center.x + inner * std::cos(angle), center.y + inner * std::sin(angle)};
                Vector2 p2 = {center.x + s * std::cos(angle), center.y + s * std::sin(angle)};
                DrawLineEx(p1, p2, 2.0f, icon_col);
            }
            break;
        }
        case NodeState::POWERPLANT: {
            // Lightning bolt
            Vector2 pts[4] = {
                {center.x + s * 0.1f, center.y - s},
                {center.x - s * 0.3f, center.y + s * 0.1f},
                {center.x + s * 0.3f, center.y - s * 0.1f},
                {center.x - s * 0.1f, center.y + s},
            };
            DrawLineEx(pts[0], pts[1], 2.0f, icon_col);
            DrawLineEx(pts[1], pts[2], 2.0f, icon_col);
            DrawLineEx(pts[2], pts[3], 2.0f, icon_col);
            break;
        }
        case NodeState::FORT: {
            // Shield: rounded triangle
            Vector2 top = {center.x, center.y - s * 0.8f};
            Vector2 bl  = {center.x - s * 0.7f, center.y + s * 0.1f};
            Vector2 br  = {center.x + s * 0.7f, center.y + s * 0.1f};
            Vector2 bot = {center.x, center.y + s * 0.9f};
            DrawLineEx(bl, top, 1.5f, icon_col);
            DrawLineEx(top, br, 1.5f, icon_col);
            DrawLineEx(br, bot, 1.5f, icon_col);
            DrawLineEx(bot, bl, 1.5f, icon_col);
            break;
        }
        case NodeState::ARTILLERY: {
            // Crosshair
            DrawLineEx({center.x - s, center.y}, {center.x + s, center.y}, 1.5f, icon_col);
            DrawLineEx({center.x, center.y - s}, {center.x, center.y + s}, 1.5f, icon_col);
            DrawCircleLinesV(center, s * 0.6f, icon_col);
            break;
        }
        default:
            break;
    }
}

void Renderer::draw_edges(const Game& game, const Camera2D_Custom& camera, float scale) {
    const auto& graph = game.graph();
    // Two-layer edges: thick dark outer, lighter inner
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx(sa, sb, rc_.edge_outer_thickness * scale, rc_.edge_outer_color);
    }
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx(sa, sb, rc_.edge_inner_thickness * scale, rc_.edge_inner_color);
    }
}

void Renderer::draw_nodes(const Game& game, const Camera2D_Custom& camera, float scale) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    float r = rc_.node_radius * scale;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        const auto& nd = nodes_data[i];
        Vector2 sp = camera.world_to_screen({node.x, node.y});

        // Filled circle colored by owner
        Color fill = color_for_player(nd.owner);
        DrawCircleV(sp, r, fill);
        DrawCircleLinesV(sp, r, Color{60, 55, 50, 100});

        // State icon inside
        draw_state_icon(nd.state, sp, r);

        // Troop counts below the node, colored per player
        struct TroopEntry { int player; int count; };
        TroopEntry entries[8];
        int n_entries = 0;
        for (int p = 0; p < static_cast<int>(nd.troops.size()) && p < MAX_PLAYERS; p++) {
            if (nd.troops[p] > 0) {
                entries[n_entries++] = {p, nd.troops[p]};
            }
        }

        if (n_entries > 0) {
            int total_w = 0;
            char bufs[8][16];
            int widths[8];
            int font_troop = std::max(6, static_cast<int>(rc_.font_size_troop * base_zoom_));
            int slash_w = MeasureText("/", font_troop);
            int space_w = MeasureText(" ", font_troop);
            for (int e = 0; e < n_entries; e++) {
                snprintf(bufs[e], sizeof(bufs[e]), "%d", entries[e].count);
                widths[e] = MeasureText(bufs[e], font_troop);
                total_w += widths[e];
                if (e > 0) total_w += slash_w + space_w * 2;
            }

            int x = static_cast<int>(sp.x) - total_w / 2;
            int y = static_cast<int>(sp.y + r + 2);
            for (int e = 0; e < n_entries; e++) {
                if (e > 0) {
                    DrawText("/", x + space_w, y, font_troop,
                             Color{80, 70, 60, 200});
                    x += slash_w + space_w * 2;
                }
                DrawText(bufs[e], x, y, font_troop,
                         color_for_player(entries[e].player));
                x += widths[e];
            }
        }
    }
}

void Renderer::draw_troop_groups(const Game& game, const Camera2D_Custom& camera, float scale) {
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
                float dot_r = (rc_.troop_dot_min_radius + frac * (rc_.troop_dot_max_radius - rc_.troop_dot_min_radius)) * scale;

                Color c = color_for_player(group.owner);
                if (group.retreating) {
                    c.a = 150;
                }
                DrawCircleV(sp, dot_r, c);

                // Troop count text below the dot
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", group.count);
                int font_grp = std::max(6, static_cast<int>((rc_.font_size_troop - 0.2f) * base_zoom_));
                int tw = MeasureText(buf, font_grp);
                DrawText(buf,
                         static_cast<int>(sp.x) - tw / 2,
                         static_cast<int>(sp.y + dot_r + 2),
                         font_grp, c);
            }
        }
    }
}
