#ifndef CRISKY_EXPANSION_AGENT_HPP
#define CRISKY_EXPANSION_AGENT_HPP

#include "ai/distribution_sub_agent.hpp"
#include "ai/model_config.hpp"

// Expansion sub-agent: scores border/unowned/enemy nodes adjacent to
// our territory, encouraging territorial growth.
class ExpansionSubAgent : public DistributionSubAgent {
public:
    explicit ExpansionSubAgent(const ModelConfig& cfg) : config_(cfg) {}

    const char* name() const override { return "expansion"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

protected:
    ModelConfig config_;
};

#endif
