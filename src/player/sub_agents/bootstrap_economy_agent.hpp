#ifndef CRISKY_BOOTSTRAP_ECONOMY_AGENT_HPP
#define CRISKY_BOOTSTRAP_ECONOMY_AGENT_HPP

#include "player/sub_agents/attention_sub_agent.hpp"
#include "player/sub_agents/economy_agent.hpp"
#include "engine/game_state.hpp"

#include <vector>

// Bootstrap economy sub-agent: creates a build plan on first tick
// (4 factories on nearest nodes, then 1 powerplant on the best-connected
// node adjacent to them), executes one at a time by concentrating all
// attention, then falls through to standard economy logic.
class BootstrapEconomySubAgent : public AttentionSubAgent {
public:
    void contribute(const Game& game, int player_id,
                    const std::vector<float>& current_attention,
                    std::vector<float>& deltas_out,
                    PlayerCommands& direct_commands_out) override;

private:
    struct BuildStep {
        int node;
        NodeState structure;
    };

    bool planned_ = false;
    std::vector<BuildStep> plan_;
    int plan_cursor_ = 0;

    // Fallback for post-bootstrap economy decisions
    EconomySubAgent fallback_;

    void generate_plan(const Game& game, int player_id);

    static constexpr float BOOTSTRAP_ATTENTION = 20.0f;
    static constexpr float BOOTSTRAP_DAMPEN = 5.0f;
    static constexpr int NUM_FACTORIES = 4;
};

#endif
