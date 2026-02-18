#ifndef CRISKY_VIZ_TABBED_PANEL_HPP
#define CRISKY_VIZ_TABBED_PANEL_HPP

#include "viz/panel.hpp"
#include "imgui.h"

#include <memory>
#include <string>
#include <vector>

// Wraps multiple panels in an ImGui tab bar within a single window.
class TabbedPanel : public Panel {
public:
    explicit TabbedPanel(std::string title) : title_(std::move(title)) {}

    void add_tab(std::unique_ptr<Panel> panel) {
        tabs_.push_back(std::move(panel));
    }

    void draw() override {
        if (ImGui::BeginTabBar(title_.c_str())) {
            for (auto& tab : tabs_) {
                if (ImGui::BeginTabItem(tab->title())) {
                    tab->draw();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    const char* title() const override { return title_.c_str(); }

private:
    std::string title_;
    std::vector<std::unique_ptr<Panel>> tabs_;
};

#endif
