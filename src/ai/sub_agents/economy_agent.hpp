#ifndef CRISKY_ECONOMY_AGENT_HPP
#define CRISKY_ECONOMY_AGENT_HPP

#include "ai/distribution_sub_agent.hpp"

// Economy sub-agent: scores nodes that need factory/powerplant balance,
// and emits build commands for structures.
class EconomySubAgent : public DistributionSubAgent {
public:
    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    float beta() const override { return 1.0f; }

private:
    static constexpr float FACTORY_POWERPLANT_SCORE = 0.8f;
    static constexpr float UNBUILT_NODE_SCORE = 5.0f;
};

#endif
