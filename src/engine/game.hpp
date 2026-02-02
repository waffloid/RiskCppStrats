#ifndef CRISKY_GAME_HPP
#define CRISKY_GAME_HPP

#include <vector>
#include <cstdint>

#include "game_config.hpp"
#include "game_state.hpp"
#include "graph.hpp"
#include "edge_lanes.hpp"
#include "player/player_interface.hpp"

class Game {
public:
    // Construct a game: generate graph, place capitals at specified nodes.
    // capitals[i] is the node index for player i's capital.
    Game(const GameConfig& config, const std::vector<int>& capitals, uint64_t seed);

    // Main tick. Deterministic given same commands.
    void tick(float dt, const std::vector<PlayerCommands>& commands);

    // State accessors
    const GameConfig& config() const { return config_; }
    const Graph& graph() const { return graph_; }
    const std::vector<NodeData>& node_data() const { return node_data_; }
    const std::vector<EdgeLanes>& edge_lanes() const { return edge_lanes_; }
    bool is_alive(int player_id) const { return alive_[player_id]; }
    int n_players() const { return n_players_; }
    float time() const { return time_; }

    bool is_game_over() const;

private:
    GameConfig config_;
    Graph graph_;
    std::vector<NodeData> node_data_;
    std::vector<EdgeLanes> edge_lanes_;
    std::vector<bool> alive_;
    float time_ = 0.0f;
    int n_players_;

    void process_build_commands(const std::vector<PlayerCommands>& commands);
    void process_troop_sends(const std::vector<PlayerCommands>& commands);
    void process_retreats(const std::vector<PlayerCommands>& commands);
    void update_all_edge_lanes(float dt);
    void process_arrivals(std::vector<Arrival>& arrivals);
    void resolve_all_combat();
    void produce_all_troops();
    void update_alive();

    // Validate that player owns the node and has enough troops
    bool validate_build(int player_id, const BuildCommand& cmd) const;
    bool validate_troop_send(int player_id, const TroopCommand& cmd) const;

    int building_cost(NodeState state) const;
};

#endif
