#include "viz/overlays/node_heatmap.hpp"
#include "raylib.h"

#include <algorithm>
#include <cmath>

NodeHeatmapOverlay::NodeHeatmapOverlay(std::string title, const Graph* graph, ValueFn value_fn)
    : title_(std::move(title)), graph_(graph), value_fn_(std::move(value_fn)) {}

void NodeHeatmapOverlay::draw() {
    if (!visible || !graph_) return;

    auto values = value_fn_();
    int n = graph_->num_nodes();
    if (static_cast<int>(values.size()) != n) return;

    // Apply normalization (reuse GraphHeatmap's static method)
    GraphHeatmap::apply_norm(values, norm);

    // Auto-range
    if (auto_range && n > 0) {
        vmin = *std::min_element(values.begin(), values.end());
        vmax = *std::max_element(values.begin(), values.end());
        if (vmax - vmin < 1e-6f) { vmin -= 0.5f; vmax += 0.5f; }
    }

    float inv_range = (vmax - vmin > 1e-6f) ? 1.0f / (vmax - vmin) : 0.0f;
    unsigned char a = static_cast<unsigned char>(alpha * 255);

    auto ramp_color = [&](float t) -> Color {
        t = std::clamp(t, 0.0f, 1.0f);
        auto v = static_cast<unsigned char>(t * 255);
        return Color{v, v, v, a};
    };

    // Draw nodes
    for (int i = 0; i < n; i++) {
        float t = std::clamp((values[i] - vmin) * inv_range, 0.0f, 1.0f);
        DrawCircle(
            static_cast<int>(graph_->nodes[i].x),
            static_cast<int>(graph_->nodes[i].y),
            radius,
            ramp_color(t)
        );
    }
}
