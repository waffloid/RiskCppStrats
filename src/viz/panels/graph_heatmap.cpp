#include "viz/panels/graph_heatmap.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>

GraphHeatmap::GraphHeatmap(std::string title, const Graph* graph, ValueFn value_fn)
    : title_(std::move(title)), graph_(graph), value_fn_(std::move(value_fn)) {}

unsigned int GraphHeatmap::value_to_color(float t) {
    // Blue (0) -> Green (0.5) -> Red (1.0)
    t = std::clamp(t, 0.0f, 1.0f);
    float r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = 0.0f;
        g = s;
        b = 1.0f - s;
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = s;
        g = 1.0f - s;
        b = 0.0f;
    }
    auto to_byte = [](float v) { return static_cast<unsigned int>(v * 255.0f); };
    return IM_COL32(to_byte(r), to_byte(g), to_byte(b), 255);
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

    // Auto-range
    if (auto_range && n > 0) {
        vmin = *std::min_element(values.begin(), values.end());
        vmax = *std::max_element(values.begin(), values.end());
        if (vmax - vmin < 1e-6f) { vmin -= 0.5f; vmax += 0.5f; }
    }

    // Get draw area
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float size = std::min(avail.x, avail.y);
    if (size < 50.0f) size = 200.0f;

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

    float margin = 12.0f;
    float scale = (size - 2 * margin) / std::max(range_x, range_y);

    auto to_screen = [&](float x, float y) -> ImVec2 {
        return ImVec2(
            origin.x + margin + (x - min_x) * scale,
            origin.y + margin + (y - min_y) * scale
        );
    };

    // Draw edges
    for (int i = 0; i < n; i++) {
        float ix = graph_->nodes[i].x, iy = graph_->nodes[i].y;
        ImVec2 p0 = to_screen(ix, iy);
        for (int nbr : graph_->neighbors(i)) {
            if (nbr <= i) continue;  // draw each edge once
            float nx = graph_->nodes[nbr].x, ny = graph_->nodes[nbr].y;
            ImVec2 p1 = to_screen(nx, ny);
            dl->AddLine(p0, p1, IM_COL32(80, 80, 80, 100), 1.0f);
        }
    }

    // Draw nodes
    float radius = std::max(3.0f, scale * 0.02f);
    for (int i = 0; i < n; i++) {
        float t = (vmax - vmin > 1e-6f) ? (values[i] - vmin) / (vmax - vmin) : 0.5f;
        unsigned int col = value_to_color(t);
        float x = graph_->nodes[i].x, y = graph_->nodes[i].y;
        ImVec2 pos = to_screen(x, y);
        dl->AddCircleFilled(pos, radius, col);
    }

    // Reserve space so ImGui layout works
    ImGui::Dummy(ImVec2(size, size));

    // Color legend
    ImGui::Text("Range: [%.3f, %.3f]", vmin, vmax);
}
