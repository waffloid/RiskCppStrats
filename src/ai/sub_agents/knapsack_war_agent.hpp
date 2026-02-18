#ifndef CRISKY_KNAPSACK_WAR_AGENT_HPP
#define CRISKY_KNAPSACK_WAR_AGENT_HPP

#include "ai/utils/war_utilities.hpp"
#include "ai/model_config.hpp"

// War sub-agent using per-node budget greedy solver with knapsack metrics.
class KnapsackWarSubAgent : public DistributionSubAgent {
public:
    explicit KnapsackWarSubAgent(const ModelConfig& cfg) : config_(cfg) {}

    const char* name() const override { return "knapsack_war"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

private:
    ModelConfig config_;
};

#endif
