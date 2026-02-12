#include "viz/panels/time_series_chart.hpp"
#include "imgui.h"
#include "implot.h"

TimeSeriesChart::TimeSeriesChart(std::string title, std::string x_label, std::string y_label)
    : title_(std::move(title)), x_label_(std::move(x_label)), y_label_(std::move(y_label)) {}

void TimeSeriesChart::add_series(std::string label, unsigned int color, RingBuffer<float>* buf) {
    series_.push_back({std::move(label), color, buf});
}

void TimeSeriesChart::draw() {
    if (ImPlot::BeginPlot(("##" + title_).c_str(), ImVec2(-1, height_))) {
        ImPlot::SetupAxes(x_label_.c_str(), y_label_.c_str());

        for (auto& s : series_) {
            if (!s.buffer || s.buffer->empty()) continue;

            auto data = s.buffer->to_vector();

            ImPlot::PushStyleColor(ImPlotCol_Line, s.color);
            ImPlot::PlotLine(s.label.c_str(), data.data(), static_cast<int>(data.size()));
            ImPlot::PopStyleColor();
        }

        ImPlot::EndPlot();
    }
}
