#ifndef CRISKY_CONDITIONAL_EXPANSION_AGENT_HPP
#define CRISKY_CONDITIONAL_EXPANSION_AGENT_HPP

#include "ai/sub_agents/expansion_agent.hpp"

// Expansion sub-agent that shuts off when too much owned territory
// is unconstructed (no factory/powerplant/capital).
class ConditionalExpansionSubAgent : public ExpansionSubAgent {
public:
    using ExpansionSubAgent::ExpansionSubAgent;

    const char* name() const override { return "conditional_expansion"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;
};

#endif
