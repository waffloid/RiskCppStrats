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

    // Auto-range
    if (auto_range && n > 0) {
        vmin = *std::min_element(values.begin(), values.end());
        vmax = *std::max_element(values.begin(), values.end());
        if (vmax - vmin < 1e-6f) { vmin -= 0.5f; vmax += 0.5f; }
    }

    for (int i = 0; i < n; i++) {
        float t = (vmax - vmin > 1e-6f) ? (values[i] - vmin) / (vmax - vmin) : 0.5f;
        t = std::clamp(t, 0.0f, 1.0f);

        // Blue -> Green -> Red ramp
        unsigned char r, g, b;
        if (t < 0.5f) {
            float s = t * 2.0f;
            r = 0;
            g = static_cast<unsigned char>(s * 255);
            b = static_cast<unsigned char>((1.0f - s) * 255);
        } else {
            float s = (t - 0.5f) * 2.0f;
            r = static_cast<unsigned char>(s * 255);
            g = static_cast<unsigned char>((1.0f - s) * 255);
            b = 0;
        }
        unsigned char a = static_cast<unsigned char>(alpha * 255);

        DrawCircle(
            static_cast<int>(graph_->nodes[i].x),
            static_cast<int>(graph_->nodes[i].y),
            radius,
            Color{r, g, b, a}
        );
    }
}
