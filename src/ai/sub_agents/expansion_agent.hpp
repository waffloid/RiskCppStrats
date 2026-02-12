#ifndef CRISKY_EXPANSION_AGENT_HPP
#define CRISKY_EXPANSION_AGENT_HPP

#include "ai/sub_agents/attention_sub_agent.hpp"

// Expansion sub-agent: drives attention toward border/unowned/enemy nodes
// adjacent to our territory, encouraging territorial growth.
class ExpansionSubAgent : public AttentionSubAgent {
public:
    void contribute(const Game& game, int player_id,
                    const std::vector<float>& current_attention,
                    std::vector<float>& deltas_out,
                    PlayerCommands& direct_commands_out) override;

private:
    static constexpr float BORDER_ATTENTION = 5.0f;
};

#endif
