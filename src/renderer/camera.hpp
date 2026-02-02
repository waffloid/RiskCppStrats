#ifndef CRISKY_CAMERA_HPP
#define CRISKY_CAMERA_HPP

#include "raylib.h"
#include "engine/graph.hpp"

class Camera2D_Custom {
public:
    Camera2D_Custom() = default;

    // Fit the graph bounding box into the window with padding
    void fit_to_graph(const Graph& graph, int screen_w, int screen_h, float padding = 40.0f);

    // Process input: scroll wheel zoom (toward mouse), middle-click drag pan
    void update();

    // Coordinate transforms
    Vector2 world_to_screen(Vector2 world) const;
    Vector2 screen_to_world(Vector2 screen) const;

    float zoom() const { return zoom_; }
    Vector2 offset() const { return offset_; }

private:
    Vector2 offset_ = {0, 0};  // world position at screen center
    float zoom_ = 1.0f;
    int screen_w_ = 800;
    int screen_h_ = 600;
    bool dragging_ = false;
    Vector2 drag_start_ = {0, 0};
    Vector2 offset_at_drag_start_ = {0, 0};
};

#endif
