#include "human_player.hpp"
#include "engine/game.hpp"
#include <cmath>

HumanPlayer::HumanPlayer() = default;

int HumanPlayer::node_at_screen_pos(Vector2 screen_pos, const Camera2D_Custom& camera,
                                     const Game& game) const {
    Vector2 world_pos = camera.screen_to_world(screen_pos);
    const float CLICK_RADIUS = 1.4f;  // slightly wider than node radius (1.05)

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

void HumanPlayer::draw_dashed_circle(Vector2 center, float radius, float thickness,
                                      Color color, int segments, float dash_ratio) {
    float angle_step = 2.0f * PI / segments;
    for (int i = 0; i < segments; i++) {
        if (i % 2 == 1) continue; // skip every other segment = dashed
        float a1 = i * angle_step;
        float a2 = a1 + angle_step * dash_ratio;
        Vector2 p1 = {center.x + cosf(a1) * radius, center.y + sinf(a1) * radius};
        Vector2 p2 = {center.x + cosf(a2) * radius, center.y + sinf(a2) * radius};
        DrawLineEx(p1, p2, thickness, color);
    }
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
    bool shift_pressed = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // Drag to select / shift-drag to union / alt-drag to deselect
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (!is_dragging_) {
            drag_start_ = world_pos;
            is_dragging_ = true;
            is_alt_dragging_ = alt_pressed;
            is_shift_dragging_ = shift_pressed;
        }
        drag_current_ = world_pos;
    } else if (is_dragging_) {
        // Released: apply selection
        float radius = std::sqrt((drag_current_.x - drag_start_.x) * (drag_current_.x - drag_start_.x) +
                                 (drag_current_.y - drag_start_.y) * (drag_current_.y - drag_start_.y));
        auto nodes_in_drag = nodes_in_circle(drag_start_, radius, game);

        if (radius > 5.0f) {
            // Actual drag (not a click)
            if (is_alt_dragging_) {
                // Alt-drag: remove nodes from selection
                for (int node : nodes_in_drag) {
                    selected_nodes_.erase(node);
                }
            } else if (is_shift_dragging_) {
                // Shift-drag: add nodes to selection (union)
                for (int node : nodes_in_drag) {
                    if (node_data[node].owner == player_id) {
                        selected_nodes_.insert(node);
                    }
                }
            } else {
                // Regular drag: replace entire selection
                selected_nodes_.clear();
                for (int node : nodes_in_drag) {
                    if (node_data[node].owner == player_id) {
                        selected_nodes_.insert(node);
                    }
                }
            }
        } else {
            // Tiny drag = click
            if (hovered_node >= 0) {
                if (alt_pressed) {
                    // Alt-click: remove from selection
                    selected_nodes_.erase(hovered_node);
                } else if (shift_pressed) {
                    // Shift-click: toggle in selection (union)
                    if (node_data[hovered_node].owner == player_id) {
                        if (selected_nodes_.count(hovered_node)) {
                            selected_nodes_.erase(hovered_node);
                        } else {
                            selected_nodes_.insert(hovered_node);
                        }
                    }
                } else {
                    // Plain click: select only this node (if we own it),
                    // otherwise treat as clicking empty space
                    if (node_data[hovered_node].owner == player_id) {
                        selected_nodes_.clear();
                        selected_nodes_.insert(hovered_node);
                    } else {
                        clear_selection();
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
    // If any selected source node is unowned, target must be owned by us
    // (prevents zipping troops through no-man's land)
    bool any_unowned_source = false;
    for (int node : selected_nodes_) {
        if (node_data[node].owner != player_id) {
            any_unowned_source = true;
            break;
        }
    }
    bool target_valid = hovered_node >= 0 && !selected_nodes_.empty() &&
        (!any_unowned_source || node_data[hovered_node].owner == player_id);

    if (target_valid) {
        int send_count = 0;

        if (IsKeyPressed(KEY_Q)) {
            send_count = troop_send_config.fixed_1;
        } else if (IsKeyPressed(KEY_E)) {
            send_count = troop_send_config.fixed_2;
        } else if (IsKeyPressed(KEY_R)) {
            // Send percentage of garrison from each selected node
            int total = 0;
            for (int node : selected_nodes_) {
                int available = std::max(0, node_data[node].troops[player_id] - 1);
                total += static_cast<int>(available * troop_send_config.percent_1);
            }
            send_count = total;
        } else if (IsKeyPressed(KEY_F)) {
            // Send percentage of garrison from each selected node
            int total = 0;
            for (int node : selected_nodes_) {
                int available = std::max(0, node_data[node].troops[player_id] - 1);
                total += static_cast<int>(available * troop_send_config.percent_2);
            }
            send_count = total;
        }

        if (send_count > 0) {
            // Queue send from all selected nodes to target
            pending_send_count_ = send_count;
            pending_send_source_ = -1;  // from all selected
        }
    }

    // Build hotkeys: 1/2/3/4 on selected nodes
    NodeState build_state = NodeState::DEFAULT;
    bool should_build = false;

    if (IsKeyPressed(KEY_ONE)) {
        build_state = NodeState::FACTORY;
        should_build = true;
    } else if (IsKeyPressed(KEY_TWO)) {
        build_state = NodeState::FORT;
        should_build = true;
    } else if (IsKeyPressed(KEY_THREE)) {
        build_state = NodeState::POWERPLANT;
        should_build = true;
    } else if (IsKeyPressed(KEY_FOUR)) {
        build_state = NodeState::ARTILLERY;
        should_build = true;
    }

    if (should_build && hovered_node >= 0) {
        pending_build_ = true;
        pending_build_node_ = hovered_node;
        pending_structure_ = build_state;
    }
}

void HumanPlayer::decide(const Game& game, int player_id, PlayerCommands& out) {
    if (!game.is_alive(player_id)) return;

    const auto& node_data = game.node_data();

    // Send queued troops from all selected nodes to target
    // Always leave at least 1 troop at each source node to keep ownership
    if (pending_send_count_ > 0 && target_node_ >= 0 && !selected_nodes_.empty()) {
        int total_available = 0;
        for (int node : selected_nodes_) {
            if (node != target_node_) {
                int available = std::max(0, node_data[node].troops[player_id] - 1);
                total_available += available;
            }
        }
        int to_send = std::min(pending_send_count_, total_available);

        if (to_send > 0) {
            // Distribute troops from selected nodes (simple: first-come-first-served)
            int remaining = to_send;
            for (int node : selected_nodes_) {
                if (node == target_node_) continue;
                int available = std::max(0, node_data[node].troops[player_id] - 1);
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

    // Build queued structure on hovered node
    if (pending_build_ && pending_build_node_ >= 0) {
        out.builds.push_back({pending_build_node_, pending_structure_});
        pending_build_ = false;
        pending_build_node_ = -1;
    }
}

void HumanPlayer::render(const Camera2D_Custom& camera, int screen_w, int screen_h,
                        const Game& game, int player_id,
                        const ColorScheme& scheme) const {
    if (!game.is_alive(player_id)) return;

    // Draw drag circle if dragging
    if (is_dragging_) {
        Vector2 start_screen = camera.world_to_screen(drag_start_);
        Vector2 current_screen = camera.world_to_screen(drag_current_);
        float radius = std::sqrt((current_screen.x - start_screen.x) * (current_screen.x - start_screen.x) +
                                (current_screen.y - start_screen.y) * (current_screen.y - start_screen.y));

        if (is_alt_dragging_) {
            // Alt-drag: muted deselect color from scheme
            Color deselect_color = scheme.node_outline;
            deselect_color.a = 180;
            draw_dashed_circle(start_screen, radius, 2.5f, deselect_color);
        } else {
            // Normal/shift drag: player color
            Color drag_color = scheme.player_colors[player_id % 8];
            drag_color.a = 160;
            draw_dashed_circle(start_screen, radius, 2.5f, drag_color);
        }
    }
}
