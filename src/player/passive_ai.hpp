#ifndef CRISKY_PASSIVE_AI_HPP
#define CRISKY_PASSIVE_AI_HPP

#include "player/player_interface.hpp"

// AI that does nothing. Useful for neutral players and benchmark baselines.
class PassiveAI : public PlayerInterface {
public:
    void decide(const Game& /*game*/, int /*player_id*/, PlayerCommands& /*out*/) override {}
};

#endif
