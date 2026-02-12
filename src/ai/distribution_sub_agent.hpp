#ifndef CRISKY_DISTRIBUTION_SUB_AGENT_HPP
#define CRISKY_DISTRIBUTION_SUB_AGENT_HPP

#include <vector>
#include "engine/player_interface.hpp"
#include "observability/ai_metrics.hpp"

class Game;

class DistributionSubAgent {
public:
    virtual ~DistributionSubAgent() = default;

    // Human-readable name for this sub-agent (used in metrics/viz).
    virtual const char* name() const = 0;

    // Produce raw scores (pre-softmax) + optional direct commands.
    // scores_out: per-node raw scores. Higher = want more troops. Unowned nodes should be 0.
    // direct_commands_out: builds, attacks, retreats that bypass the distribution field.
    virtual void score(const Game& game, int player_id,
                       std::vector<float>& scores_out,
                       PlayerCommands& direct_commands_out) = 0;

    // Softmax temperature. Higher β = more peaked (troops concentrate at top nodes).
    virtual float beta() const { return 1.0f; }

    // Optional metrics output pointer (set by orchestrator before calling score).
    AIMetricsSnapshot* metrics_out = nullptr;
};

#endif
