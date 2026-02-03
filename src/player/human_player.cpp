#include "human_player.hpp"
#include "engine/game.hpp"
#include <cmath>

HumanPlayer::HumanPlayer() = default;

int HumanPlayer::node_at_screen_pos(Vector2 screen_pos, const Camera2D_Custom& camera,
                                     const Game& game) const {
    Vector2 world_pos = camera.screen_to_world(screen_pos);
    const float CLICK_RADIUS = 20.0f;

    const auto& graph = game.graph();
    int closest_node = -1;
    float closest_dist = CLICK_RADIUS;

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        float dx = node.x - world_pos.x;
        float dy = node.y - world_pos.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < closest_dist) {
            closest_dist = dist;
            closest_node = i;
        }
    }
    return closest_node;
}

std::set<int> HumanPlayer::nodes_in_circle(Vector2 world_center, float radius,
                                          const Game& game) const {
    std::set<int> result;
    const auto& graph = game.graph();

    for (int i = 0; i < graph.num_nodes(); i++) {
        const auto& node = graph.nodes[i];
        float dx = node.x - world_center.x;
        float dy = node.y - world_center.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= radius) {
            result.insert(i);
        }
    }
    return result;
}

void HumanPlayer::clear_selection() {
    selected_nodes_.clear();
    target_node_ = -1;
}

void HumanPlayer::process_input(const Camera2D_Custom& camera, int screen_w, int screen_h,
                                const Game& game, int player_id) {
    if (!game.is_alive(player_id)) return;

    Vector2 mouse_pos = GetMousePosition();
    Vector2 world_pos = camera.screen_to_world(mouse_pos);
    int hovered_node = node_at_screen_pos(mouse_pos, camera, game);
    target_node_ = hovered_node;

    const auto& node_data = game.node_data();
    bool alt_pressed = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    // Drag to select / alt-drag to deselect
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (!is_dragging_) {
            drag_start_ = world_pos;
            is_dragging_ = true;
            is_alt_dragging_ = alt_pressed;
        }
        drag_current_ = world_pos;
    } else if (is_dragging_) {
        // Released: apply selection
        float radius = std::sqrt((drag_current_.x - drag_start_.x) * (drag_current_.x - drag_start_.x) +
                                 (drag_current_.y - drag_start_.y) * (drag_current_.y - drag_start_.y));
        auto nodes_in_drag = nodes_in_circle(drag_start_, radius, game);

        if (is_alt_dragging_) {
            // Alt-drag: deselect nodes
            for (int node : nodes_in_drag) {
                selected_nodes_.erase(node);
            }
        } else if (radius > 5.0f) {
            // Regular drag: add nodes (that we own) to selection
            for (int node : nodes_in_drag) {
                if (node_data[node].owner == player_id) {
                    selected_nodes_.insert(node);
                }
            }
        } else {
            // Tiny drag = click
            if (hovered_node >= 0) {
                if (alt_pressed) {
                    // Alt-click: deselect
                    selected_nodes_.erase(hovered_node);
                } else {
                    // Click: toggle select (if own it)
                    if (node_data[hovered_node].owner == player_id) {
                        if (selected_nodes_.count(hovered_node)) {
                            selected_nodes_.erase(hovered_node);
                        } else {
                            selected_nodes_.insert(hovered_node);
                        }
                    }
                }
            } else {
                // Click empty space: clear selection
                clear_selection();
            }
        }
        is_dragging_ = false;
    }

    // Troop send hotkeys (Q, E, R, F) — send from selected nodes to target
    if (hovered_node >= 0 && !selected_nodes_.empty()) {
        int send_count = 0;

        if (IsKeyPressed(KEY_Q)) {
            send_count = troop_send_config.fixed_1;
        } else if (IsKeyPressed(KEY_E)) {
            send_count = troop_send_config.fixed_2;
        } else if (IsKeyPressed(KEY_R)) {
            // Send percentage of garrison from each selected node
            int total = 0;
            for (int node : selected_nodes_) {
                total += static_cast<int>(node_data[node].troops[player_id] *
                                         troop_send_config.percent_1);
            }
            send_count = total;
        } else if (IsKeyPressed(KEY_F)) {
            // Send percentage of garrison from each selected node
            int total = 0;
            for (int node : selected_nodes_) {
                total += static_cast<int>(node_data[node].troops[player_id] *
                                         troop_send_config.percent_2);
            }
            send_count = total;
        }

        if (send_count > 0 && hovered_node != -1 && selected_nodes_.count(hovered_node) == 0) {
            // Queue send from all selected nodes to target
            pending_send_count_ = send_count;
            pending_send_source_ = -1;  // from all selected
        }
    }

    // Build hotkeys: B/P/F/A on selected nodes (builds on all)
    NodeState build_state = NodeState::DEFAULT;
    bool should_build = false;

    if (IsKeyPressed(KEY_B)) {
        build_state = NodeState::FACTORY;
        should_build = true;
    } else if (IsKeyPressed(KEY_P)) {
        build_state = NodeState::POWERPLANT;
        should_build = true;
    } else if (IsKeyPressed(KEY_X)) {
        build_state = NodeState::FORT;
        should_build = true;
    } else if (IsKeyPressed(KEY_C)) {
        build_state = NodeState::ARTILLERY;
        should_build = true;
    }

    if (should_build && !selected_nodes_.empty()) {
        pending_build_ = true;
        pending_structure_ = build_state;
    }
}

void HumanPlayer::decide(const Game& game, int player_id, PlayerCommands& out) {
    if (!game.is_alive(player_id)) return;

    const auto& node_data = game.node_data();

    // Send queued troops from all selected nodes to target
    if (pending_send_count_ > 0 && target_node_ >= 0 && !selected_nodes_.empty()) {
        int total_available = 0;
        for (int node : selected_nodes_) {
            if (node != target_node_) {
                total_available += node_data[node].troops[player_id];
            }
        }
        int to_send = std::min(pending_send_count_, total_available);

        if (to_send > 0) {
            // Distribute troops from selected nodes (simple: first-come-first-served)
            int remaining = to_send;
            for (int node : selected_nodes_) {
                if (node == target_node_) continue;
                int available = node_data[node].troops[player_id];
                int send = std::min(remaining, available);
                if (send > 0) {
                    out.troops.push_back({node, target_node_, send});
                    remaining -= send;
                }
                if (remaining <= 0) break;
            }
        }
        pending_send_count_ = 0;
    }

    // Build queued structures on all selected nodes
    if (pending_build_ && !selected_nodes_.empty()) {
        for (int node : selected_nodes_) {
            out.builds.push_back({node, pending_structure_});
        }
        pending_build_ = false;
    }
}

void HumanPlayer::render(const Camera2D_Custom& camera, int screen_w, int screen_h,
                        const Game& game, int player_id) const {
    if (!game.is_alive(player_id)) return;

    const auto& graph = game.graph();

    // Draw white outlines on selected nodes
    for (int node_idx : selected_nodes_) {
        const auto& node = graph.nodes[node_idx];
        Vector2 screen_pos = camera.world_to_screen({node.x, node.y});
        DrawCircleLines((int)screen_pos.x, (int)screen_pos.y, 18, WHITE);
    }

    // Draw drag circle if dragging
    if (is_dragging_) {
        Vector2 start_screen = camera.world_to_screen(drag_start_);
        Vector2 current_screen = camera.world_to_screen(drag_current_);
        float radius = std::sqrt((current_screen.x - start_screen.x) * (current_screen.x - start_screen.x) +
                                (current_screen.y - start_screen.y) * (current_screen.y - start_screen.y));

        Color circle_color = is_alt_dragging_ ? Color{255, 0, 0, 100} : Color{255, 255, 255, 100};
        DrawCircleLines((int)start_screen.x, (int)start_screen.y, radius, circle_color);
    }
}
