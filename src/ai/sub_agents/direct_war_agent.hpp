#ifndef CRISKY_DIRECT_WAR_AGENT_HPP
#define CRISKY_DIRECT_WAR_AGENT_HPP

#include "ai/utils/war_utilities.hpp"
#include "ai/model_config.hpp"

// War sub-agent: scores front-line nodes facing real enemies,
// emits direct attack commands via v2 solver, and retreats uncommitted troops.
class DirectWarSubAgent : public DistributionSubAgent {
public:
    explicit DirectWarSubAgent(const ModelConfig& cfg) : config_(cfg) {}

    const char* name() const override { return "direct_war"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

private:
    ModelConfig config_;
};

#endif
