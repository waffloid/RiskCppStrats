#ifndef CRISKY_VIZ_PANEL_HOST_HPP
#define CRISKY_VIZ_PANEL_HOST_HPP

#include "viz/panel.hpp"

#include <memory>
#include <string>
#include <vector>

// Manages a collection of ImGui panels and draws them each frame.
// Panels are dockable ImGui windows; the host calls ImGui::Begin/End for each.
class PanelHost {
public:
    void add(std::unique_ptr<Panel> panel);

    // Draw all panels. Call once per frame inside the rlImGui begin/end block.
    void draw();

    // Number of registered panels.
    int count() const { return static_cast<int>(panels_.size()); }

private:
    struct PanelSlot {
        std::unique_ptr<Panel> panel;
        bool open = true;
    };
    std::vector<PanelSlot> panels_;
};

#endif
