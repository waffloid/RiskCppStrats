#ifndef CRISKY_EXPANSION_AGENT_HPP
#define CRISKY_EXPANSION_AGENT_HPP

#include "ai/distribution_sub_agent.hpp"

// Expansion sub-agent: scores border/unowned/enemy nodes adjacent to
// our territory, encouraging territorial growth.
class ExpansionSubAgent : public DistributionSubAgent {
public:
    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    float beta() const override { return 1.0f; }

private:
    static constexpr float BORDER_SCORE = 5.0f;
};

#endif
