#include "renderer/renderer.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

// --- HSL <-> RGB helpers for zen mode ---

struct HSL { float h, s, l; };

static float hue2rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static HSL rgb_to_hsl(Color c) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    float h = 0.0f, s = 0.0f, l = (mx + mn) * 0.5f;
    if (mx != mn) {
        float d = mx - mn;
        s = (l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    return {h, s, l};
}

static Color hsl_to_rgb(HSL hsl, unsigned char a = 255) {
    float r, g, b;
    if (hsl.s <= 0.0f) {
        r = g = b = hsl.l;
    } else {
        float q = (hsl.l < 0.5f) ? hsl.l * (1.0f + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
        float p = 2.0f * hsl.l - q;
        r = hue2rgb(p, q, hsl.h + 1.0f / 3.0f);
        g = hue2rgb(p, q, hsl.h);
        b = hue2rgb(p, q, hsl.h - 1.0f / 3.0f);
    }
    return Color{
        static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
        a
    };
}

Renderer::Renderer(int screen_w, int screen_h, const ColorScheme* scheme)
    : screen_w_(screen_w), screen_h_(screen_h), scheme_(scheme)
{
}

void Renderer::draw(const Game& game, const Camera2D_Custom& camera,
                    const std::set<int>* selected_nodes) {
    if (base_zoom_ <= 0.0f) base_zoom_ = camera.zoom();
    float scale = camera.zoom() / base_zoom_;

    // Precompute total troops for zen mode brightness sigmoid (real players only)
    // Smoothed with EMA to prevent frame-to-frame jitter
    if (zen_mode_) {
        int raw_total = 0;
        int n_real = game.n_real_players();
        const auto& nodes_data = game.node_data();
        for (int i = 0; i < game.graph().num_nodes(); i++) {
            const auto& t = nodes_data[i].troops;
            for (int p = 0; p < n_real && p < static_cast<int>(t.size()); p++)
                raw_total += t[p];
        }
        for (const auto& el : game.edge_lanes()) {
            for (int lane = 0; lane < 2; lane++) {
                for (const auto& g : el.lanes[lane].groups) {
                    if (g.owner >= 0 && g.owner < n_real)
                        raw_total += g.count;
                }
            }
        }
        float raw = static_cast<float>(raw_total);
        if (zen_total_troops_ <= 0.0f) {
            zen_total_troops_ = raw; // first frame: seed directly
        } else {
            // Smooth: ~10 second time constant at 60fps
            constexpr float alpha = 0.0015f;
            zen_total_troops_ += alpha * (raw - zen_total_troops_);
        }
    }

    // Layer order: edges → troop groups → node circles → selection halos → labels
    draw_edges(game, camera, scale);
    if (!zen_mode_) draw_troop_groups(game, camera, scale);
    draw_node_circles(game, camera, scale);
    if (selected_nodes && !selected_nodes->empty()) {
        draw_selection_halos(game, camera, scale, *selected_nodes);
    }
    if (!zen_mode_) draw_node_labels(game, camera, scale);
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

// Find dominant player and purity from a troop vector.
// Returns {dominant_index, max_frac} or {-1, 0} if empty.
static std::pair<int, float> find_dominant(const int* troops, int n_players) {
    int total = 0, dominant = -1, max_t = 0;
    for (int p = 0; p < n_players; p++) {
        total += troops[p];
        if (troops[p] > max_t) { max_t = troops[p]; dominant = p; }
    }
    if (total == 0 || dominant < 0) return {-1, 0.0f};
    return {dominant, static_cast<float>(max_t) / static_cast<float>(total)};
}

// Sigmoid: maps count to [0,1] relative to game average
float Renderer::zen_sigmoid(int count, int num_nodes) const {
    float avg = (num_nodes > 0 && zen_total_troops_ > 0.0f)
        ? zen_total_troops_ / static_cast<float>(num_nodes) * 0.5f
        : 1.0f;
    return static_cast<float>(count) / (static_cast<float>(count) + avg);
}

Color Renderer::zen_node_color(const int* troops, int n_players, int num_nodes) const {
    auto [dominant, max_frac] = find_dominant(troops, n_players);
    if (dominant < 0) return Color{0, 0, 0, 255};

    int total = 0;
    for (int p = 0; p < n_players; p++) total += troops[p];

    HSL hsl = rgb_to_hsl(color_for_player(dominant));
    hsl.s *= std::pow(max_frac, 1.5f);

    // Brightness: node's share of total troops, rescaled by #nodes.
    // share=1.0 means "average", >1 means above average.
    // Sigmoid maps: 0→0, average→0.5, 2x average→0.67, big→~1.0
    float share = (zen_total_troops_ > 0.0f)
        ? static_cast<float>(total) / zen_total_troops_ * static_cast<float>(num_nodes)
        : 0.0f;
    float intensity = share / (share + 1.0f);
    hsl.l = intensity * std::max(hsl.l, 0.65f);
    return hsl_to_rgb(hsl);
}

Color Renderer::zen_edge_color(const int* hue_troops, int n_players,
                                int transit_total, int num_nodes) const {
    constexpr float baseline = 0.05f;
    auto [dominant, max_frac] = find_dominant(hue_troops, n_players);
    if (dominant < 0) {
        HSL hsl = rgb_to_hsl(scheme_->unowned_node);
        hsl.l = baseline;
        return hsl_to_rgb(hsl);
    }

    HSL hsl = rgb_to_hsl(color_for_player(dominant));
    hsl.s *= std::pow(max_frac, 1.5f);
    // Brightness: transit's share of total troops, rescaled by #nodes (same scale as nodes)
    float share = (zen_total_troops_ > 0.0f)
        ? static_cast<float>(transit_total) / zen_total_troops_ * static_cast<float>(num_nodes)
        : 0.0f;
    float intensity = share / (share + 1.0f);
    float max_l = std::max(hsl.l, 0.65f);
    hsl.l = baseline + intensity * (max_l - baseline);
    return hsl_to_rgb(hsl);
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

    if (zen_mode_) {
        const auto& nodes_data = game.node_data();
        const auto& all_el = game.edge_lanes();
        int n_real = game.n_real_players();
        int num_nodes = graph.num_nodes();
        size_t n_edges = graph.edges.size();

        if (zen_edge_troops_.size() != n_edges) {
            zen_edge_troops_.resize(n_edges);
            zen_edge_transit_.assign(n_edges, -1.0f);
        }

        constexpr float ema_alpha = 0.08f;

        for (size_t ei = 0; ei < n_edges; ei++) {
            const auto& edge = graph.edges[ei];
            const auto& na = graph.nodes[edge.a_idx];
            const auto& nb = graph.nodes[edge.b_idx];
            Vector2 sa = camera.world_to_screen({na.x, na.y});
            Vector2 sb = camera.world_to_screen({nb.x, nb.y});

            // Gather transit troops on this edge (real players only)
            int transit[MAX_PLAYERS] = {};
            int transit_total = 0;
            for (const auto& el : all_el) {
                if (el.edge_idx == static_cast<int>(ei)) {
                    for (int lane = 0; lane < 2; lane++)
                        for (const auto& g : el.lanes[lane].groups)
                            if (g.owner >= 0 && g.owner < n_real) {
                                transit[g.owner] += g.count;
                                transit_total += g.count;
                            }
                    break;
                }
            }

            // Hue vector: endpoints (halved) + transit
            int np = std::min(n_real, MAX_PLAYERS);
            int hue_troops[MAX_PLAYERS] = {};
            for (int p = 0; p < np; p++) {
                int ta = (p < static_cast<int>(nodes_data[edge.a_idx].troops.size())) ? nodes_data[edge.a_idx].troops[p] : 0;
                int tb = (p < static_cast<int>(nodes_data[edge.b_idx].troops.size())) ? nodes_data[edge.b_idx].troops[p] : 0;
                hue_troops[p] = (ta + tb) / 2 + transit[p];
            }

            // EMA the hue troops and transit total, then compute color
            zen_edge_troops_[ei].update(hue_troops, np, ema_alpha);
            float ft = static_cast<float>(transit_total);
            if (zen_edge_transit_[ei] < 0.0f) zen_edge_transit_[ei] = ft;
            else zen_edge_transit_[ei] += ema_alpha * (ft - zen_edge_transit_[ei]);

            int smoothed_hue[MAX_PLAYERS] = {};
            zen_edge_troops_[ei].get(smoothed_hue, np);
            int smoothed_transit = static_cast<int>(zen_edge_transit_[ei]);

            Color col = zen_edge_color(smoothed_hue, np, smoothed_transit, num_nodes);
            DrawLineEx({sa.x + sox, sa.y + soy}, {sb.x + sox, sb.y + soy},
                       rc_.edge_outer_thickness * scale, scheme_->shadow);
            DrawLineEx(sa, sb, rc_.edge_outer_thickness * scale, col);
        }
        return;
    }

    // Normal mode
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

    // Initialize per-node smoothed troops for zen mode
    if (zen_mode_ && static_cast<int>(zen_node_troops_.size()) != graph.num_nodes())
        zen_node_troops_.resize(graph.num_nodes());

    constexpr float troop_ema_alpha = 0.08f;

    // Node circles, outlines, and state icons
    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        const auto& nd = nodes_data[i];
        Vector2 sp = camera.world_to_screen({node.x, node.y});

        float sox = rc_.shadow_offset_x;
        float soy = rc_.shadow_offset_y;
        DrawCircleV({sp.x + sox, sp.y + soy}, r, scheme_->shadow);

        Color fill;
        if (zen_mode_) {
            int raw[MAX_PLAYERS] = {};
            int n_real = game.n_real_players();
            int np = std::min(n_real, MAX_PLAYERS);
            for (int p = 0; p < np && p < static_cast<int>(nd.troops.size()); p++)
                raw[p] = nd.troops[p];
            zen_node_troops_[i].update(raw, np, troop_ema_alpha);
            int smoothed[MAX_PLAYERS] = {};
            zen_node_troops_[i].get(smoothed, np);
            fill = zen_node_color(smoothed, np, graph.num_nodes());
        } else {
            fill = color_for_player(nd.owner);
        }
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
