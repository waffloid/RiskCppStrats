#ifndef CRISKY_DISTRIBUTION_AI_PLAYER_HPP
#define CRISKY_DISTRIBUTION_AI_PLAYER_HPP

#include <memory>
#include <vector>
#include "engine/player_interface.hpp"
#include "ai/distribution_sub_agent.hpp"
#include "ai/distribution.hpp"
#include "observability/ai_metrics.hpp"

struct DistributionSubAgentSlot {
    std::unique_ptr<DistributionSubAgent> agent;
    float weight;
};

class DistributionAIPlayer : public PlayerInterface {
public:
    explicit DistributionAIPlayer(int player_id);

    // Construct with no sub-agents (add your own via add_sub_agent).
    struct NoDefaults {};
    DistributionAIPlayer(int player_id, NoDefaults);

    void add_sub_agent(std::unique_ptr<DistributionSubAgent> agent, float weight);

    void decide(const Game& game, int player_id, PlayerCommands& out) override;

    // Read-only access to the distribution field (for visualization/debugging).
    const TroopDistribution& distribution() const { return prev_distribution_; }

    // Observability
    const AIMetricsSnapshot& metrics() const { return metrics_; }
    void set_metrics_enabled(bool on) { metrics_enabled_ = on; }

    // EMA smoothing factor (tunable, e.g. by GA)
    float ema_alpha = 0.3f;

private:
    int player_id_;
    bool initialized_ = false;

    std::vector<DistributionSubAgentSlot> sub_agents_;

    // Persistent distribution (smoothed via EMA across ticks)
    TroopDistribution prev_distribution_;

    // Minimum troops to send per transport command
    static constexpr int MIN_TROOPS_TO_SEND = 5;

    void execute_transport(const Game& game, PlayerCommands& out,
                           const std::vector<float>& gradient,
                           const std::vector<bool>& masked);

    // Observability
    AIMetricsSnapshot metrics_;
    bool metrics_enabled_ = false;

    // Scratch buffers (reused across ticks)
    std::vector<float> scratch_scores_;
    std::vector<bool> scratch_masked_;
};

#endif
