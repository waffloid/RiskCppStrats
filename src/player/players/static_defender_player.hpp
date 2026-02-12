#ifndef CRISKY_STATIC_DEFENDER_PLAYER_HPP
#define CRISKY_STATIC_DEFENDER_PLAYER_HPP

#include "engine/player_interface.hpp"
#include "engine/game.hpp"

// Player that never attacks but builds forts on owned nodes when affordable.
class StaticDefenderPlayer : public PlayerInterface {
public:
    void decide(const Game& game, int player_id, PlayerCommands& out) override {
        if (!game.is_alive(player_id)) return;

        const auto& nodes_data = game.node_data();
        const auto& graph = game.graph();
        const int cost_fort = game.config().cost_fort;

        for (int node = 0; node < graph.num_nodes(); node++) {
            const auto& nd = nodes_data[node];
            if (nd.owner != player_id) continue;
            if (nd.state != NodeState::DEFAULT) continue;

            int troops = nd.troops[player_id];
            if (troops > cost_fort + 1) {
                out.builds.push_back({node, NodeState::FORT});
            }
        }
    }
};

#endif
