#include "viz/panels/time_series_chart.hpp"
#include "imgui.h"
#include "implot.h"

TimeSeriesChart::TimeSeriesChart(std::string title, std::string x_label, std::string y_label)
    : title_(std::move(title)), x_label_(std::move(x_label)), y_label_(std::move(y_label)) {}

void TimeSeriesChart::add_series(std::string label, unsigned int color, RingBuffer<float>* buf) {
    series_.push_back({std::move(label), color, buf});
}

void TimeSeriesChart::set_series_color(int index, unsigned int color) {
    if (index >= 0 && index < static_cast<int>(series_.size()))
        series_[index].color = color;
}

void TimeSeriesChart::draw() {
    ImGui::Checkbox("Follow", &follow_);
    float h = ImGui::GetContentRegionAvail().y;
    if (h < 50.0f) h = height_;
    if (ImPlot::BeginPlot(("##" + title_).c_str(), ImVec2(-1, h))) {
        int x_flags = follow_ ? ImPlotAxisFlags_AutoFit : 0;
        ImPlot::SetupAxes(x_label_.c_str(), y_label_.c_str(),
                          x_flags, ImPlotAxisFlags_AutoFit);

        for (auto& s : series_) {
            if (!s.buffer || s.buffer->empty()) continue;

            auto data = s.buffer->to_vector();

            ImPlotSpec spec;
            spec.LineColor = ImGui::ColorConvertU32ToFloat4(s.color);
            ImPlot::PlotLine(s.label.c_str(), data.data(), static_cast<int>(data.size()), 1.0, 0.0, spec);
        }

        ImPlot::EndPlot();
    }
}
