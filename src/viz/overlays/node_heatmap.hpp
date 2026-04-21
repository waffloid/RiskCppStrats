#ifndef CRISKY_VIZ_NODE_HEATMAP_HPP
#define CRISKY_VIZ_NODE_HEATMAP_HPP

#include "viz/panel.hpp"
#include "viz/panels/graph_heatmap.hpp"  // HeatmapNorm enum
#include "engine/graph.hpp"

#include <functional>
#include <string>
#include <vector>

// World-space overlay: colors game map nodes by a per-node value.
// Drawn with raylib primitives inside the game camera transform.
class NodeHeatmapOverlay : public Overlay {
public:
    using ValueFn = std::function<std::vector<float>()>;

    NodeHeatmapOverlay(std::string title, const Graph* graph, ValueFn value_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

    float vmin = 0.0f;
    float vmax = 1.0f;
    bool auto_range = true;
    float radius = 12.0f;
    float alpha = 0.6f;
    HeatmapNorm norm = HeatmapNorm::Log;  // default Log for overlays

private:
    std::string title_;
    const Graph* graph_;
    ValueFn value_fn_;
};

#endif
