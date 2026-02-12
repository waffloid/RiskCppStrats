#ifndef CRISKY_DISTRIBUTION_AI_PLAYER_HPP
#define CRISKY_DISTRIBUTION_AI_PLAYER_HPP

#include <memory>
#include <vector>
#include "engine/player_interface.hpp"
#include "ai/distribution_sub_agent.hpp"
#include "ai/distribution.hpp"
#include "ai/model_config.hpp"
#include "systems/transport/potential_solvers.hpp"
#include "observability/ai_metrics.hpp"

struct DistributionSubAgentSlot {
    std::unique_ptr<DistributionSubAgent> agent;
    float weight;
};

class DistributionAIPlayer : public PlayerInterface {
public:
    // Construct with default sub-agents (Economy + Expansion).
    explicit DistributionAIPlayer(int player_id);

    // Construct with no sub-agents (add your own via add_sub_agent).
    struct NoDefaults {};
    DistributionAIPlayer(int player_id, NoDefaults);

    // Construct with explicit config and no default sub-agents.
    DistributionAIPlayer(int player_id, const ModelConfig& cfg);

    void add_sub_agent(std::unique_ptr<DistributionSubAgent> agent, float weight);

    void decide(const Game& game, int player_id, PlayerCommands& out) override;

    // Read-only access to the distribution field (for visualization/debugging).
    const TroopDistribution& distribution() const { return prev_distribution_; }

    // Read-only access to sub-agents (for visualization).
    const std::vector<DistributionSubAgentSlot>& sub_agents() const { return sub_agents_; }

    // Observability
    const AIMetricsSnapshot& metrics() const { return metrics_; }
    const AIDecisionSnapshot& decision_snapshot() const { return decision_snapshot_; }
    void set_metrics_enabled(bool on) { metrics_enabled_ = on; }

    // Config access
    const ModelConfig& config() const { return config_; }

    // Inject a custom potential solver (default: potential_deficit).
    void set_potential_solver(PotentialSolver solver) { potential_solver_ = std::move(solver); }

private:
    int player_id_;
    ModelConfig config_;
    PotentialSolver potential_solver_ = potential_deficit;
    bool initialized_ = false;

    std::vector<DistributionSubAgentSlot> sub_agents_;

    // Persistent distribution (smoothed via EMA across ticks)
    TroopDistribution prev_distribution_;

    void execute_transport(const Game& game, PlayerCommands& out,
                           const std::vector<float>& potential,
                           const std::vector<bool>& masked);

    // Observability
    AIMetricsSnapshot metrics_;
    AIDecisionSnapshot decision_snapshot_;
    bool metrics_enabled_ = false;

    // Scratch buffers (reused across ticks)
    std::vector<float> scratch_scores_;
    std::vector<bool> scratch_masked_;
};

#endif
