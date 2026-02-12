#ifndef CRISKY_ATTENTION_AI_PLAYER_HPP
#define CRISKY_ATTENTION_AI_PLAYER_HPP

#include <memory>
#include <vector>
#include "engine/player_interface.hpp"
#include "ai/sub_agents/attention_sub_agent.hpp"
#include "observability/ai_metrics.hpp"

struct SubAgentSlot {
    std::unique_ptr<AttentionSubAgent> agent;
    float weight;
};

class AttentionAIPlayer : public PlayerInterface {
public:
    // Construct with default sub-agents (economy + expansion, weight 1.0 each).
    explicit AttentionAIPlayer(int player_id);

    // Construct with no sub-agents (add your own via add_sub_agent).
    struct NoDefaults {};
    AttentionAIPlayer(int player_id, NoDefaults);

    // Add a sub-agent with a given weight.
    void add_sub_agent(std::unique_ptr<AttentionSubAgent> agent, float weight);

    void decide(const Game& game, int player_id, PlayerCommands& out) override;

    // Read-only access to the attention field (for visualization/debugging).
    const std::vector<float>& attention() const { return attention_; }

    // Observability: per-tick AI metrics snapshot.
    const AIMetricsSnapshot& metrics() const { return metrics_; }
    void set_metrics_enabled(bool on) { metrics_enabled_ = on; }

private:
    // Persistent state
    std::vector<float> attention_;
    int player_id_;
    bool initialized_ = false;

    // Sub-agents
    std::vector<SubAgentSlot> sub_agents_;

    // Constants
    static constexpr float DIFFUSION_RATE = 0.02f;
    static constexpr float OUTFLOW_RATE = 0.1f;
    static constexpr int   MIN_TROOPS_TO_SEND = 5;

    void diffuse_attention(const Game& game);
    void execute_troop_flow(const Game& game, PlayerCommands& out,
                            const std::vector<bool>& masked);

    // Observability
    AIMetricsSnapshot metrics_;
    bool metrics_enabled_ = false;
    void compute_attention_metrics(const Game& game, int player_id, int n);

    // Scratch buffers (reused across ticks to avoid per-tick heap allocations)
    std::vector<float> scratch_total_deltas_;
    std::vector<float> scratch_sub_deltas_;
    std::vector<float> scratch_lap_;
    std::vector<bool> scratch_masked_;
};

#endif
