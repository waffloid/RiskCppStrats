#include "viz/overlays/gradient_arrows.hpp"
#include "raylib.h"

#include <algorithm>
#include <cmath>

GradientArrowsOverlay::GradientArrowsOverlay(std::string title, const Graph* graph, GradientFn gradient_fn)
    : title_(std::move(title)), graph_(graph), gradient_fn_(std::move(gradient_fn)) {}

void GradientArrowsOverlay::draw() {
    if (!visible || !graph_) return;

    auto gradient = gradient_fn_();
    int n = graph_->num_nodes();
    if (static_cast<int>(gradient.size()) != n) return;

    // For each node with negative gradient (excess), draw an arrow toward
    // the neighbor with the highest positive gradient (deficit).
    for (int i = 0; i < n; i++) {
        if (gradient[i] >= 0.0f) continue;  // not excess

        // Find best neighbor with positive gradient
        int best = -1;
        float best_val = 0.0f;
        for (int nbr : graph_->neighbors(i)) {
            if (gradient[nbr] > best_val) {
                best_val = gradient[nbr];
                best = nbr;
            }
        }

        if (best < 0 || best_val < min_gradient) continue;

        float x0 = graph_->nodes[i].x;
        float y0 = graph_->nodes[i].y;
        float x1 = graph_->nodes[best].x;
        float y1 = graph_->nodes[best].y;

        // Shorten the line slightly so it doesn't overlap node circles
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) continue;
        float nx = dx / len, ny = dy / len;
        float shrink = 10.0f;
        x0 += nx * shrink;
        y0 += ny * shrink;
        x1 -= nx * shrink;
        y1 -= ny * shrink;

        float thickness = std::clamp(best_val * arrow_scale * 0.1f, 1.0f, 5.0f);

        DrawLineEx(
            Vector2{x0, y0},
            Vector2{x1, y1},
            thickness,
            Color{255, 200, 0, 180}
        );

        // Arrowhead
        float ah = 8.0f;
        float px = x1 - nx * ah + ny * ah * 0.5f;
        float py = y1 - ny * ah - nx * ah * 0.5f;
        float qx = x1 - nx * ah - ny * ah * 0.5f;
        float qy = y1 - ny * ah + nx * ah * 0.5f;
        DrawTriangle(
            Vector2{x1, y1},
            Vector2{qx, qy},
            Vector2{px, py},
            Color{255, 200, 0, 200}
        );
    }
}
