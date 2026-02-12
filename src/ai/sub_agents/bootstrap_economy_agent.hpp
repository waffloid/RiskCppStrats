#ifndef CRISKY_BOOTSTRAP_ECONOMY_AGENT_HPP
#define CRISKY_BOOTSTRAP_ECONOMY_AGENT_HPP

#include "ai/distribution_sub_agent.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "engine/game_state.hpp"

#include <vector>

// Bootstrap economy sub-agent: creates a build plan on first tick
// (4 factories on nearest nodes, then 1 powerplant on the best-connected
// node adjacent to them), executes one at a time by concentrating all
// score on the target, then falls through to standard economy logic.
class BootstrapEconomySubAgent : public DistributionSubAgent {
public:
    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    float beta() const override { return bootstrapping_ ? 5.0f : 1.0f; }

private:
    struct BuildStep {
        int node;
        NodeState structure;
    };

    bool planned_ = false;
    bool bootstrapping_ = true;
    std::vector<BuildStep> plan_;
    int plan_cursor_ = 0;

    // Fallback for post-bootstrap economy decisions
    EconomySubAgent fallback_;

    void generate_plan(const Game& game, int player_id);

    static constexpr float BOOTSTRAP_SCORE = 20.0f;
    static constexpr int NUM_FACTORIES = 4;
};

#endif
