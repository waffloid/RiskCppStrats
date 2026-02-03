#ifndef CRISKY_HUMAN_PLAYER_HPP
#define CRISKY_HUMAN_PLAYER_HPP

#include "player_interface.hpp"
#include "renderer/camera.hpp"
#include <raylib.h>
#include <set>

// Configuration for troop send hotkeys (Q, E, R, F)
struct TroopSendConfig {
    int fixed_1 = 50;          // Q: fixed amount
    int fixed_2 = 250;         // E: fixed amount
    float percent_1 = 0.5f;    // R: percentage of garrison
    float percent_2 = 1.0f;    // F: percentage of garrison
};

class HumanPlayer : public PlayerInterface {
public:
    HumanPlayer();
    virtual ~HumanPlayer() = default;

    // Called once per frame to update input state
    void process_input(const Camera2D_Custom& camera, int screen_w, int screen_h,
                       const class Game& game, int player_id);

    // Called once per tick to generate commands from accumulated input
    virtual void decide(const class Game& game, int player_id, PlayerCommands& out) override;

    // Render UI: selected nodes, drag circle, etc.
    void render(const Camera2D_Custom& camera, int screen_w, int screen_h,
               const class Game& game, int player_id) const;

    // Config
    TroopSendConfig troop_send_config;

private:
    std::set<int> selected_nodes_;     // currently selected nodes
    int target_node_ = -1;             // hovered target node
    int pending_send_count_ = 0;       // troops queued to send
    int pending_send_source_ = -1;     // source node for queued send
    bool pending_build_ = false;       // queue a structure build
    NodeState pending_structure_ = NodeState::DEFAULT;

    // Drag state
    Vector2 drag_start_ = {0, 0};
    Vector2 drag_current_ = {0, 0};
    bool is_dragging_ = false;
    bool is_alt_dragging_ = false;  // alt-drag deselects

    // Helper: find node at screen position
    int node_at_screen_pos(Vector2 screen_pos, const Camera2D_Custom& camera,
                          const class Game& game) const;

    // Helper: find all nodes in circle
    std::set<int> nodes_in_circle(Vector2 world_center, float radius,
                                 const class Game& game) const;

    // Helper: clear selection
    void clear_selection();
};

#endif
