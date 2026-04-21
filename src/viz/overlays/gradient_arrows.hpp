#ifndef CRISKY_VIZ_GRADIENT_ARROWS_HPP
#define CRISKY_VIZ_GRADIENT_ARROWS_HPP

#include "viz/panel.hpp"
#include "engine/graph.hpp"

#include <functional>
#include <string>
#include <vector>

// World-space overlay: draws arrows from excess→deficit nodes along the gradient.
// Arrow thickness proportional to gradient magnitude.
class GradientArrowsOverlay : public Overlay {
public:
    using GradientFn = std::function<std::vector<float>()>;

    GradientArrowsOverlay(std::string title, const Graph* graph, GradientFn gradient_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

    float min_gradient = 1.0f;   // minimum gradient diff to draw an arrow
    float arrow_scale = 2.0f;    // scale factor for arrow thickness

private:
    std::string title_;
    const Graph* graph_;
    GradientFn gradient_fn_;
};

#endif
