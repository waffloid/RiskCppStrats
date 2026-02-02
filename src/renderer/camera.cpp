#include "renderer/camera.hpp"
#include <algorithm>
#include <cmath>

void Camera2D_Custom::fit_to_graph(const Graph& graph, int screen_w, int screen_h, float padding) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;

    if (graph.nodes.empty()) {
        offset_ = {screen_w * 0.5f, screen_h * 0.5f};
        zoom_ = 1.0f;
        return;
    }

    float min_x = graph.nodes[0].x, max_x = graph.nodes[0].x;
    float min_y = graph.nodes[0].y, max_y = graph.nodes[0].y;
    for (const auto& n : graph.nodes) {
        min_x = std::min(min_x, n.x);
        max_x = std::max(max_x, n.x);
        min_y = std::min(min_y, n.y);
        max_y = std::max(max_y, n.y);
    }

    float graph_w = max_x - min_x;
    float graph_h = max_y - min_y;
    if (graph_w < 1.0f) graph_w = 1.0f;
    if (graph_h < 1.0f) graph_h = 1.0f;

    float avail_w = screen_w - 2.0f * padding;
    float avail_h = screen_h - 2.0f * padding;

    zoom_ = std::min(avail_w / graph_w, avail_h / graph_h);

    // Center of graph in world space
    float cx = (min_x + max_x) * 0.5f;
    float cy = (min_y + max_y) * 0.5f;
    offset_ = {cx, cy};
}

void Camera2D_Custom::update() {
    // Zoom toward mouse position
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 mouse_screen = GetMousePosition();
        Vector2 mouse_world = screen_to_world(mouse_screen);

        float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
        zoom_ *= factor;
        zoom_ = std::clamp(zoom_, 0.1f, 100.0f);

        // After zoom, adjust offset so mouse_world stays under the cursor
        // screen_to_world(mouse_screen) should still equal mouse_world
        // mouse_world = offset_ + (mouse_screen - screen_center) / zoom_
        // offset_ = mouse_world - (mouse_screen - screen_center) / zoom_
        Vector2 screen_center = {screen_w_ * 0.5f, screen_h_ * 0.5f};
        offset_.x = mouse_world.x - (mouse_screen.x - screen_center.x) / zoom_;
        offset_.y = mouse_world.y - (mouse_screen.y - screen_center.y) / zoom_;
    }

    // Middle-click drag pan
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        dragging_ = true;
        drag_start_ = GetMousePosition();
        offset_at_drag_start_ = offset_;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
        dragging_ = false;
    }
    if (dragging_) {
        Vector2 mouse = GetMousePosition();
        float dx = mouse.x - drag_start_.x;
        float dy = mouse.y - drag_start_.y;
        offset_.x = offset_at_drag_start_.x - dx / zoom_;
        offset_.y = offset_at_drag_start_.y - dy / zoom_;
    }

    // WASD pan
    float pan_speed = 300.0f / zoom_;
    float dt = GetFrameTime();
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    offset_.y -= pan_speed * dt;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   offset_.y += pan_speed * dt;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   offset_.x -= pan_speed * dt;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  offset_.x += pan_speed * dt;
}

Vector2 Camera2D_Custom::world_to_screen(Vector2 world) const {
    Vector2 screen_center = {screen_w_ * 0.5f, screen_h_ * 0.5f};
    return {
        screen_center.x + (world.x - offset_.x) * zoom_,
        screen_center.y + (world.y - offset_.y) * zoom_
    };
}

Vector2 Camera2D_Custom::screen_to_world(Vector2 screen) const {
    Vector2 screen_center = {screen_w_ * 0.5f, screen_h_ * 0.5f};
    return {
        offset_.x + (screen.x - screen_center.x) / zoom_,
        offset_.y + (screen.y - screen_center.y) / zoom_
    };
}
