#ifndef CRISKY_VIZ_GRAPH_HEATMAP_HPP
#define CRISKY_VIZ_GRAPH_HEATMAP_HPP

#include "viz/panel.hpp"
#include "engine/graph.hpp"

#include <functional>
#include <string>
#include <vector>

// Normalization mode for heatmap values before color mapping.
enum class HeatmapNorm {
    Linear,  // Direct min-max mapping
    Log,     // log(1 + |x|), sign-preserving. Expands small differences.
    Rank,    // Map by rank order (percentile). Uniform visual spread.
};

// Mini-map panel that draws graph nodes colored by a per-node value.
// Used to visualize distributions, gradients, sub-agent scores, etc.
class GraphHeatmap : public Panel {
public:
    // value_fn: returns per-node float values (size = num_nodes). Called each frame.
    using ValueFn = std::function<std::vector<float>()>;
    // Optional: per-edge float [0,1] for edge coloring (size = num_edges).
    using EdgeValueFn = std::function<std::vector<float>()>;
    // Optional: per-node direct color (bypasses heatmap ramp).
    using NodeColorFn = std::function<std::vector<unsigned int>()>;

    GraphHeatmap(std::string title, const Graph* graph, ValueFn value_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

    // Set optional edge value callback. Values in [0,1]; 0=gray, 1=bright.
    void set_edge_value_fn(EdgeValueFn fn) { edge_value_fn_ = std::move(fn); }
    // Set optional node color callback. Bypasses heatmap ramp entirely.
    void set_node_color_fn(NodeColorFn fn) { node_color_fn_ = std::move(fn); }

    // Color ramp bounds. Values are clamped to [vmin, vmax] before coloring.
    float vmin = 0.0f;
    float vmax = 1.0f;
    bool auto_range = true;  // if true, vmin/vmax computed from data each frame
    HeatmapNorm norm = HeatmapNorm::Linear;

    // Apply normalization transform to raw values in-place
    static void apply_norm(std::vector<float>& values, HeatmapNorm mode);

private:
    std::string title_;
    const Graph* graph_;
    ValueFn value_fn_;
    EdgeValueFn edge_value_fn_;
    NodeColorFn node_color_fn_;

    // Map node value [0,1] to greyscale (black -> white)
    static unsigned int value_to_color(float t);
};

#endif
