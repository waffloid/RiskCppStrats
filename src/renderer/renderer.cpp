#include "renderer/renderer.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

Renderer::Renderer(int screen_w, int screen_h, const ColorScheme* scheme)
    : screen_w_(screen_w), screen_h_(screen_h), scheme_(scheme)
{
}

void Renderer::draw(const Game& game, const Camera2D_Custom& camera,
                    const std::set<int>* selected_nodes) {
    if (base_zoom_ <= 0.0f) base_zoom_ = camera.zoom();
    float scale = camera.zoom() / base_zoom_;

    // Layer order: edges → troop groups → node circles → selection halos → labels
    draw_edges(game, camera, scale);
    draw_troop_groups(game, camera, scale);
    draw_node_circles(game, camera, scale);
    if (selected_nodes && !selected_nodes->empty()) {
        draw_selection_halos(game, camera, scale, *selected_nodes);
    }
    draw_node_labels(game, camera, scale);
}

void Renderer::draw_selection_halos(const Game& game, const Camera2D_Custom& camera,
                                     float scale, const std::set<int>& selected_nodes) {
    const auto& graph = game.graph();
    Color sys = scheme_->sys_color();
    float r = rc_.node_radius * scale;

    for (int node_idx : selected_nodes) {
        const auto& node = graph.nodes[node_idx];
        Vector2 sp = camera.world_to_screen({node.x, node.y});
        // Thick halo that touches the node edge and extends outward
        float thickness = std::max(2.0f, r * 0.25f);
        DrawRing(sp, r, r + thickness, 0.0f, 360.0f, 64, sys);
    }
}

void Renderer::draw_background(int screen_w, int screen_h,
                                const Camera2D_Custom& camera, Texture2D noise_tex) {
    // Optional multi-stop gradient layer underneath
    if ((scheme_->effect & EFFECT_GRADIENT_BG) && scheme_->gradient_num_stops >= 2) {
        int n = scheme_->gradient_num_stops;
        float band_h = static_cast<float>(screen_h) / (n - 1);
        for (int i = 0; i < n - 1; i++) {
            int y0 = static_cast<int>(i * band_h);
            int y1 = static_cast<int>((i + 1) * band_h);
            if (i == n - 2) y1 = screen_h; // ensure last band reaches bottom
            DrawRectangleGradientV(0, y0, screen_w, y1 - y0,
                                   scheme_->gradient_stops[i],
                                   scheme_->gradient_stops[i + 1]);
        }
    }

    // Noise texture — use DrawTexturePro with repeat wrapping to avoid tile seams
    Vector2 cam_off = camera.offset();
    float tile = static_cast<float>(noise_tex.width);
    float src_x = -std::fmod(cam_off.x * camera.zoom(), tile);
    float src_y = -std::fmod(cam_off.y * camera.zoom(), tile);
    // Normalize to [0, tile)
    src_x = std::fmod(src_x, tile);
    if (src_x < 0) src_x += tile;
    src_y = std::fmod(src_y, tile);
    if (src_y < 0) src_y += tile;

    Rectangle src = {src_x, src_y, static_cast<float>(screen_w), static_cast<float>(screen_h)};
    Rectangle dst = {0, 0, static_cast<float>(screen_w), static_cast<float>(screen_h)};
    Color tint = (scheme_->effect & EFFECT_GRADIENT_BG)
                     ? Color{255, 255, 255, 100}
                     : WHITE;
    DrawTexturePro(noise_tex, src, dst, {0, 0}, 0.0f, tint);
}

void Renderer::draw_scanlines(int screen_w, int screen_h) {
    if (scheme_->effect & EFFECT_SCANLINES) {
        for (int y = 0; y < screen_h; y += 3) {
            DrawRectangle(0, y, screen_w, 1, Color{0, 0, 0, 40});
        }
    }
}

Color Renderer::color_for_player(int owner) const {
    if (owner < 0 || owner >= MAX_PLAYERS) return scheme_->unowned_node;
    return scheme_->player_colors[owner];
}

void Renderer::draw_state_icon(NodeState state, Vector2 center, float radius) const {
    Color icon_col = scheme_->sys_color();
    float s = radius * 0.55f;

    switch (state) {
        case NodeState::CAPITAL: {
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
    float sox = rc_.shadow_offset_x;
    float soy = rc_.shadow_offset_y;
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx({sa.x + sox, sa.y + soy}, {sb.x + sox, sb.y + soy},
                   rc_.edge_outer_thickness * scale, scheme_->shadow);
    }
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx(sa, sb, rc_.edge_outer_thickness * scale, scheme_->edge_outer);
    }
    for (const auto& edge : graph.edges) {
        const auto& na = graph.nodes[edge.a_idx];
        const auto& nb = graph.nodes[edge.b_idx];
        Vector2 sa = camera.world_to_screen({na.x, na.y});
        Vector2 sb = camera.world_to_screen({nb.x, nb.y});
        DrawLineEx(sa, sb, rc_.edge_inner_thickness * scale, scheme_->edge_inner);
    }
}

void Renderer::draw_node_circles(const Game& game, const Camera2D_Custom& camera, float scale) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    float r = rc_.node_radius * scale;

    // Glow pass (Cyberpunk / Retrowave)
    if (scheme_->effect & EFFECT_GLOW) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < graph.num_nodes(); i++) {
            const auto& node = graph.nodes[i];
            const auto& nd = nodes_data[i];
            if (nd.owner < 0) continue;

            Vector2 sp = camera.world_to_screen({node.x, node.y});
            Color glow = color_for_player(nd.owner);
            glow.a = static_cast<unsigned char>(scheme_->glow_intensity * 255);

            DrawCircleGradient(static_cast<int>(sp.x), static_cast<int>(sp.y),
                               r * scheme_->glow_radius_mult,
                               glow, BLANK);
        }
        EndBlendMode();
    }

    // Node circles, outlines, and state icons
    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        const auto& nd = nodes_data[i];
        Vector2 sp = camera.world_to_screen({node.x, node.y});

        float sox = rc_.shadow_offset_x;
        float soy = rc_.shadow_offset_y;
        DrawCircleV({sp.x + sox, sp.y + soy}, r, scheme_->shadow);

        Color fill = color_for_player(nd.owner);
        DrawCircleV(sp, r, fill);
        DrawCircleLinesV(sp, r, scheme_->node_outline);

        draw_state_icon(nd.state, sp, r);
    }
}

void Renderer::draw_node_labels(const Game& game, const Camera2D_Custom& camera, float scale) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    float r = rc_.node_radius * scale;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        const auto& nd = nodes_data[i];
        Vector2 sp = camera.world_to_screen({node.x, node.y});

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
                    draw_outlined_text("/", x + space_w, y, font_troop, scheme_->text_separator);
                    x += slash_w + space_w * 2;
                }
                draw_outlined_text(bufs[e], x, y, font_troop,
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

                float frac = std::log2f(static_cast<float>(group.count + 1)) / 12.0f;
                frac = std::clamp(frac, 0.0f, 1.0f);
                float dot_r = (rc_.troop_dot_min_radius + frac * (rc_.troop_dot_max_radius - rc_.troop_dot_min_radius)) * scale;

                float sox = rc_.shadow_offset_x;
                float soy = rc_.shadow_offset_y;
                DrawCircleV({sp.x + sox, sp.y + soy}, dot_r, scheme_->shadow);

                Color c = color_for_player(group.owner);
                if (group.retreating) {
                    c.a = 150;
                }
                DrawCircleV(sp, dot_r, c);

                char buf[32];
                snprintf(buf, sizeof(buf), "%d", group.count);
                int font_grp = std::max(6, static_cast<int>((rc_.font_size_troop - 0.2f) * base_zoom_));
                int tw = MeasureText(buf, font_grp);
                int tx = static_cast<int>(sp.x) - tw / 2;
                int ty = static_cast<int>(sp.y + dot_r + 2);
                draw_outlined_text(buf, tx, ty, font_grp, c);
            }
        }
    }
}

void Renderer::draw_outlined_text(const char* text, int x, int y, int font_size, Color fg) const {
    // Outline contrasts with the foreground text color
    float lum = 0.299f * fg.r + 0.587f * fg.g + 0.114f * fg.b;
    Color outline = (lum > 75.0f) ? Color{0, 0, 0, 128} : Color{255, 255, 255, 128};
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            DrawText(text, x + dx, y + dy, font_size, outline);
        }
    }
    DrawText(text, x, y, font_size, fg);
}
