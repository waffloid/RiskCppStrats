#include "viz/panels/graph_heatmap.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <numeric>

GraphHeatmap::GraphHeatmap(std::string title, const Graph* graph, ValueFn value_fn)
    : title_(std::move(title)), graph_(graph), value_fn_(std::move(value_fn)) {}

unsigned int GraphHeatmap::value_to_color(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto v = static_cast<unsigned int>(t * 255.0f);
    return IM_COL32(v, v, v, 255);
}

void GraphHeatmap::apply_norm(std::vector<float>& values, HeatmapNorm mode) {
    if (mode == HeatmapNorm::Linear) return;

    if (mode == HeatmapNorm::Log) {
        for (float& v : values) {
            // Sign-preserving log: preserves relative ordering, expands small diffs
            float sign = (v >= 0.0f) ? 1.0f : -1.0f;
            v = sign * std::log1p(std::fabs(v));
        }
    } else if (mode == HeatmapNorm::Rank) {
        int n = static_cast<int>(values.size());
        // Sort indices by value, assign rank as new value
        std::vector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return values[a] < values[b];
        });
        std::vector<float> ranked(n);
        for (int i = 0; i < n; i++) {
            ranked[idx[i]] = static_cast<float>(i) / static_cast<float>(std::max(1, n - 1));
        }
        values = std::move(ranked);
    }
}

void GraphHeatmap::draw() {
    if (!graph_) {
        ImGui::TextDisabled("No graph");
        return;
    }

    auto values = value_fn_();
    int n = graph_->num_nodes();
    if (static_cast<int>(values.size()) != n) {
        ImGui::TextDisabled("Value size mismatch");
        return;
    }

    // Norm mode selector
    const char* norm_labels[] = { "Linear", "Log", "Rank" };
    int norm_idx = static_cast<int>(norm);
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::Combo("Norm", &norm_idx, norm_labels, 3)) {
        norm = static_cast<HeatmapNorm>(norm_idx);
    }

    // Apply normalization transform before auto-range
    apply_norm(values, norm);

    // Auto-range
    if (auto_range && n > 0) {
        vmin = *std::min_element(values.begin(), values.end());
        vmax = *std::max_element(values.begin(), values.end());
        if (vmax - vmin < 1e-6f) { vmin -= 0.5f; vmax += 0.5f; }
    }

    // Get draw area — fill the entire available region
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float draw_w = avail.x;
    float draw_h = avail.y - 20.0f;  // reserve space for legend text
    if (draw_w < 50.0f) draw_w = 200.0f;
    if (draw_h < 50.0f) draw_h = 200.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Compute layout bounds from node positions
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (int i = 0; i < n; i++) {
        float x = graph_->nodes[i].x, y = graph_->nodes[i].y;
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    if (range_x < 1e-6f) range_x = 1.0f;
    if (range_y < 1e-6f) range_y = 1.0f;

    // Fit graph into available rect while preserving aspect ratio
    float margin = 12.0f;
    float scale_x = (draw_w - 2 * margin) / range_x;
    float scale_y = (draw_h - 2 * margin) / range_y;
    float scale = std::min(scale_x, scale_y);

    // Center the graph in the available area
    float used_w = range_x * scale;
    float used_h = range_y * scale;
    float off_x = (draw_w - used_w) * 0.5f;
    float off_y = (draw_h - used_h) * 0.5f;

    auto to_screen = [&](float x, float y) -> ImVec2 {
        return ImVec2(
            origin.x + off_x + (x - min_x) * scale,
            origin.y + off_y + (y - min_y) * scale
        );
    };

    // Draw edges
    std::vector<float> edge_vals;
    if (edge_value_fn_) edge_vals = edge_value_fn_();

    if (!edge_vals.empty() && static_cast<int>(edge_vals.size()) == static_cast<int>(graph_->edges.size())) {
        // Edge coloring from callback
        for (const auto& e : graph_->edges) {
            ImVec2 p0 = to_screen(graph_->nodes[e.a_idx].x, graph_->nodes[e.a_idx].y);
            ImVec2 p1 = to_screen(graph_->nodes[e.b_idx].x, graph_->nodes[e.b_idx].y);
            float ev = std::clamp(edge_vals[e.idx], 0.0f, 1.0f);
            // 0 = dim gray, 1 = bright yellow
            auto r = static_cast<unsigned int>(80 + ev * 175);
            auto g = static_cast<unsigned int>(80 + ev * 175);
            auto b = static_cast<unsigned int>(80 * (1.0f - ev));
            auto a = static_cast<unsigned int>(100 + ev * 155);
            float thickness = 1.0f + ev * 2.0f;
            dl->AddLine(p0, p1, IM_COL32(r, g, b, a), thickness);
        }
    } else {
        // Default flat gray edges
        for (int i = 0; i < n; i++) {
            ImVec2 p0 = to_screen(graph_->nodes[i].x, graph_->nodes[i].y);
            for (int nbr : graph_->neighbors(i)) {
                if (nbr <= i) continue;
                ImVec2 p1 = to_screen(graph_->nodes[nbr].x, graph_->nodes[nbr].y);
                dl->AddLine(p0, p1, IM_COL32(80, 80, 80, 100), 1.0f);
            }
        }
    }

    // Draw nodes
    float radius = std::max(3.0f, scale * 0.02f);
    std::vector<unsigned int> node_colors;
    if (node_color_fn_) node_colors = node_color_fn_();

    if (!node_colors.empty() && static_cast<int>(node_colors.size()) == n) {
        // Direct node colors from callback
        for (int i = 0; i < n; i++) {
            ImVec2 pos = to_screen(graph_->nodes[i].x, graph_->nodes[i].y);
            dl->AddCircleFilled(pos, radius, node_colors[i]);
        }
    } else {
        // Default heatmap ramp
        float inv_range = (vmax - vmin > 1e-6f) ? 1.0f / (vmax - vmin) : 0.0f;
        for (int i = 0; i < n; i++) {
            float t = std::clamp((values[i] - vmin) * inv_range, 0.0f, 1.0f);
            ImVec2 pos = to_screen(graph_->nodes[i].x, graph_->nodes[i].y);
            dl->AddCircleFilled(pos, radius, value_to_color(t));
        }
    }

    // Reserve space so ImGui layout works
    ImGui::Dummy(ImVec2(draw_w, draw_h));

    // Color legend
    ImGui::Text("Range: [%.3f, %.3f]", vmin, vmax);
}
