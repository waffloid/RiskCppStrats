#include "renderer/camera.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

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
    // Zoom: scroll wheel toward mouse, I/O keys toward center
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 mouse_screen = GetMousePosition();
        Vector2 mouse_world = screen_to_world(mouse_screen);

        float factor = (wheel > 0) ? 1.1f : 1.0f / 1.1f;
        zoom_ *= factor;
        zoom_ = std::clamp(zoom_, 0.1f, 100.0f);

        Vector2 new_world = screen_to_world(mouse_screen);
        offset_.x -= (new_world.x - mouse_world.x);
        offset_.y -= (new_world.y - mouse_world.y);
    }
    {
        float key_zoom = 0.0f;
        if (IsKeyDown(KEY_I)) key_zoom += 0.5f;
        if (IsKeyDown(KEY_O)) key_zoom -= 0.5f;
        if (key_zoom != 0.0f) {
            float factor = (key_zoom > 0) ? 1.05f : 1.0f / 1.05f;
            zoom_ *= factor;
            zoom_ = std::clamp(zoom_, 0.1f, 100.0f);
        }
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

    // WASD pan (aligned with camera rotation)
    float pan_speed = 75.0f;
    float dt = GetFrameTime();
    float sx = 0.0f, sy = 0.0f;  // screen-space direction
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    sy -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   sy += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   sx -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  sx += 1.0f;
    if (sx != 0.0f || sy != 0.0f) {
        float rad = -rotation_ * (std::numbers::pi_v<float> / 180.0f);
        float cos_r = std::cos(rad);
        float sin_r = std::sin(rad);
        float wx = sx * cos_r - sy * sin_r;
        float wy = sx * sin_r + sy * cos_r;
        offset_.x += wx * pan_speed * dt;
        offset_.y += wy * pan_speed * dt;
    }

    // K/L rotation
    float rot_speed = 90.0f; // degrees per second
    if (IsKeyDown(KEY_K)) rotation_ += rot_speed * dt;
    if (IsKeyDown(KEY_L)) rotation_ -= rot_speed * dt;
}

Vector2 Camera2D_Custom::world_to_screen(Vector2 world) const {
    Vector2 screen_center = {screen_w_ * 0.5f, screen_h_ * 0.5f};
    float dx = (world.x - offset_.x) * zoom_;
    float dy = (world.y - offset_.y) * zoom_;
    float rad = rotation_ * (std::numbers::pi_v<float> / 180.0f);
    float cos_r = std::cos(rad);
    float sin_r = std::sin(rad);
    return {
        screen_center.x + dx * cos_r - dy * sin_r,
        screen_center.y + dx * sin_r + dy * cos_r
    };
}

Vector2 Camera2D_Custom::screen_to_world(Vector2 screen) const {
    Vector2 screen_center = {screen_w_ * 0.5f, screen_h_ * 0.5f};
    float sx = screen.x - screen_center.x;
    float sy = screen.y - screen_center.y;
    float rad = -rotation_ * (std::numbers::pi_v<float> / 180.0f);
    float cos_r = std::cos(rad);
    float sin_r = std::sin(rad);
    float rx = sx * cos_r - sy * sin_r;
    float ry = sx * sin_r + sy * cos_r;
    return {
        offset_.x + rx / zoom_,
        offset_.y + ry / zoom_
    };
}
