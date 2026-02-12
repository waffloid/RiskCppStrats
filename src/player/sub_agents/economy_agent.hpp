#ifndef CRISKY_ECONOMY_AGENT_HPP
#define CRISKY_ECONOMY_AGENT_HPP

#include "player/sub_agents/attention_sub_agent.hpp"

// Economy sub-agent: drives attention toward nodes that need factory/powerplant
// balance, and emits build commands for structures.
class EconomySubAgent : public AttentionSubAgent {
public:
    void contribute(const Game& game, int player_id,
                    const std::vector<float>& current_attention,
                    std::vector<float>& deltas_out,
                    PlayerCommands& direct_commands_out) override;

private:
    static constexpr float FACTORY_POWERPLANT_DELTA_DESIRE = 0.8f;
    static constexpr float UNBUILT_NODE_ATTENTION = 5.0f;
};

#endif
