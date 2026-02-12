#ifndef CRISKY_VIZ_TIME_SERIES_CHART_HPP
#define CRISKY_VIZ_TIME_SERIES_CHART_HPP

#include "viz/panel.hpp"
#include "viz/ring_buffer.hpp"

#include <string>
#include <vector>

// Generic time-series line chart panel.
// Plots N named series from RingBuffer<float>* sources using ImPlot.
class TimeSeriesChart : public Panel {
public:
    struct Series {
        std::string label;
        unsigned int color;          // ImGui color (ABGR packed)
        RingBuffer<float>* buffer;   // non-owning
    };

    TimeSeriesChart(std::string title, std::string x_label, std::string y_label);

    void add_series(std::string label, unsigned int color, RingBuffer<float>* buf);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

private:
    std::string title_;
    std::string x_label_;
    std::string y_label_;
    std::vector<Series> series_;
    float height_ = 200.0f;
};

#endif
