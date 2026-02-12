#include "viz/panel_host.hpp"
#include "imgui.h"

void PanelHost::add(std::unique_ptr<Panel> panel) {
    panels_.push_back({std::move(panel), true});
}

void PanelHost::draw() {
    for (auto& slot : panels_) {
        if (!slot.open) continue;
        if (ImGui::Begin(slot.panel->title(), &slot.open)) {
            slot.panel->draw();
        }
        ImGui::End();
    }
}
