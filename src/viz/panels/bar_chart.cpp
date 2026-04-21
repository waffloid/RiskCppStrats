#include "viz/panels/bar_chart.hpp"
#include "imgui.h"
#include "implot.h"

#include <vector>

BarChart::BarChart(std::string title, std::string y_label, DataFn data_fn)
    : title_(std::move(title)), y_label_(std::move(y_label)), data_fn_(std::move(data_fn)) {}

void BarChart::draw() {
    auto data = data_fn_();
    if (data.empty()) return;

    // Prepare arrays for ImPlot
    std::vector<double> values;
    std::vector<double> positions;
    std::vector<const char*> labels;
    values.reserve(data.size());
    positions.reserve(data.size());
    labels.reserve(data.size());
    for (int i = 0; i < static_cast<int>(data.size()); i++) {
        labels.push_back(data[i].first.c_str());
        values.push_back(static_cast<double>(data[i].second));
        positions.push_back(static_cast<double>(i));
    }

    if (ImPlot::BeginPlot(("##" + title_).c_str(), ImVec2(-1, height_))) {
        ImPlot::SetupAxes(nullptr, y_label_.c_str());
        ImPlot::SetupAxisTicks(ImAxis_X1, positions.data(), static_cast<int>(labels.size()), labels.data());

        ImPlot::PlotBars("##bars", values.data(), static_cast<int>(values.size()));
        ImPlot::EndPlot();
    }
}
