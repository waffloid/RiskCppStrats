#ifndef CRISKY_PASSIVE_PLAYER_HPP
#define CRISKY_PASSIVE_PLAYER_HPP

#include "player/player_interface.hpp"

// Player that does nothing. Useful for neutral players and benchmark baselines.
class PassivePlayer : public PlayerInterface {
public:
    void decide(const Game& /*game*/, int /*player_id*/, PlayerCommands& /*out*/) override {}
};

#endif
