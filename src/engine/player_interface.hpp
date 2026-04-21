#ifndef CRISKY_PLAYER_INTERFACE_HPP
#define CRISKY_PLAYER_INTERFACE_HPP

#include <vector>
#include "engine/game_state.hpp"

struct TroopCommand {
    int from_node;
    int to_node;      // global destination
    int count;
};

struct BuildCommand {
    int node_idx;
    NodeState structure;
};

struct RetreatCommand {
    int node_idx;     // retreat all owned troops heading away from this node
};

struct PlayerCommands {
    std::vector<TroopCommand> troops;
    std::vector<BuildCommand> builds;
    std::vector<RetreatCommand> retreats;
};

// Forward declaration — Game defined in engine/game.hpp
class Game;

class PlayerInterface {
public:
    virtual ~PlayerInterface() = default;
    virtual void decide(const Game& game, int player_id, PlayerCommands& out) = 0;
};

#endif
