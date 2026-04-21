#ifndef CRISKY_VIZ_BAR_CHART_HPP
#define CRISKY_VIZ_BAR_CHART_HPP

#include "viz/panel.hpp"

#include <functional>
#include <string>
#include <vector>

// Generic bar chart panel.
// Draws vertical bars from a data source callback.
class BarChart : public Panel {
public:
    // data_fn returns {label, value} pairs. Called each frame.
    using DataFn = std::function<std::vector<std::pair<std::string, float>>()>;

    BarChart(std::string title, std::string y_label, DataFn data_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

private:
    std::string title_;
    std::string y_label_;
    DataFn data_fn_;
    float height_ = 200.0f;
};

#endif
