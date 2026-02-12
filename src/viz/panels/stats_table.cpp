#include "viz/panels/stats_table.hpp"
#include "imgui.h"

StatsTable::StatsTable(std::string title, DataFn data_fn)
    : title_(std::move(title)), data_fn_(std::move(data_fn)) {}

void StatsTable::draw() {
    auto rows = data_fn_();
    if (rows.empty()) {
        ImGui::TextDisabled("No data");
        return;
    }

    if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (const auto& [name, value] : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        }

        ImGui::EndTable();
    }
}
