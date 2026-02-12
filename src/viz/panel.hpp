#ifndef CRISKY_VIZ_PANEL_HPP
#define CRISKY_VIZ_PANEL_HPP

#include <string>

// Base interface for ImGui panels (screen-space debug windows).
// Each panel draws itself inside an ImGui window.
class Panel {
public:
    virtual ~Panel() = default;

    // Draw the panel contents. Called inside ImGui::Begin/End by PanelHost.
    virtual void draw() = 0;

    // Display name (used as ImGui window title).
    virtual const char* title() const = 0;
};

// Base interface for raylib world-space overlays (drawn on the game map).
class Overlay {
public:
    virtual ~Overlay() = default;

    // Draw overlay using raylib primitives (called between BeginMode2D/EndMode2D).
    virtual void draw() = 0;

    virtual const char* title() const = 0;

    bool visible = true;
};

#endif
