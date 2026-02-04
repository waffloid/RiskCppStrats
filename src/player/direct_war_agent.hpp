#ifndef CRISKY_DIRECT_WAR_AGENT_HPP
#define CRISKY_DIRECT_WAR_AGENT_HPP

#include "player/war_utilities.hpp"

// War sub-agent forked from KnapsackWarSubAgent for independent iteration
// on retreat logic and attack commitment. Uses shared war utilities.
class DirectWarSubAgent : public AttentionSubAgent {
public:
    void contribute(const Game& game, int player_id,
                    const std::vector<float>& current_attention,
                    std::vector<float>& deltas_out,
                    PlayerCommands& direct_commands_out) override;

private:
    static constexpr float FRONT_LINE_ATTENTION = 5.0f;
};

#endif
