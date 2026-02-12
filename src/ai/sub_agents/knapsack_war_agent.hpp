#ifndef CRISKY_KNAPSACK_WAR_AGENT_HPP
#define CRISKY_KNAPSACK_WAR_AGENT_HPP

#include "ai/utils/war_utilities.hpp"

// War sub-agent using per-node budget greedy solver with knapsack metrics.
class KnapsackWarSubAgent : public DistributionSubAgent {
public:
    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    float beta() const override { return 2.0f; }

private:
    static constexpr float FRONT_LINE_SCORE = 5.0f;
};

#endif
