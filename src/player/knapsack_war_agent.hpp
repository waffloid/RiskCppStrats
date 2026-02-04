#ifndef CRISKY_KNAPSACK_WAR_AGENT_HPP
#define CRISKY_KNAPSACK_WAR_AGENT_HPP

#include "player/war_utilities.hpp"

// War sub-agent that uses a per-node budget greedy solver to decide which
// opposing frontier nodes to attack. Emits TroopCommands directly.
class KnapsackWarSubAgent : public AttentionSubAgent {
public:
    void contribute(const Game& game, int player_id,
                    const std::vector<float>& current_attention,
                    std::vector<float>& deltas_out,
                    PlayerCommands& direct_commands_out) override;

private:
    static constexpr float FRONT_LINE_ATTENTION = 5.0f;
};

#endif
