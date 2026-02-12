#ifndef CRISKY_VIZ_GRAPH_HEATMAP_HPP
#define CRISKY_VIZ_GRAPH_HEATMAP_HPP

#include "viz/panel.hpp"
#include "engine/graph.hpp"

#include <functional>
#include <string>
#include <vector>

// Mini-map panel that draws graph nodes colored by a per-node value.
// Used to visualize distributions, gradients, sub-agent scores, etc.
class GraphHeatmap : public Panel {
public:
    // value_fn: returns per-node float values (size = num_nodes). Called each frame.
    using ValueFn = std::function<std::vector<float>()>;

    GraphHeatmap(std::string title, const Graph* graph, ValueFn value_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

    // Color ramp bounds. Values are clamped to [vmin, vmax] before coloring.
    float vmin = 0.0f;
    float vmax = 1.0f;
    bool auto_range = true;  // if true, vmin/vmax computed from data each frame

private:
    std::string title_;
    const Graph* graph_;
    ValueFn value_fn_;

    // Map node value [0,1] to color (blue -> green -> red)
    static unsigned int value_to_color(float t);
};

#endif
