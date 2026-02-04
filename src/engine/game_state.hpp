#ifndef CRISKY_GAME_STATE_HPP
#define CRISKY_GAME_STATE_HPP

#include <cstdint>
#include <vector>

enum class NodeState : uint8_t {
    DEFAULT,
    CAPITAL,
    FACTORY,
    POWERPLANT,
    FORT,
    ARTILLERY
};

struct NodeData {
    NodeState state = NodeState::DEFAULT;
    int owner = -1;              // -1 = unowned
    std::vector<int> troops;     // troops[player_id] = count at this node
    std::vector<float> accumulated_damage;  // fractional combat damage carried between ticks
};

#endif
