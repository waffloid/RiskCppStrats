#ifndef CRISKY_ATTENTION_SUB_AGENT_HPP
#define CRISKY_ATTENTION_SUB_AGENT_HPP

#include <vector>
#include "engine/player_interface.hpp"

class Game;
struct AIMetricsSnapshot;

// Interface for sub-agents that contribute attention deltas
// (and optionally direct commands) to the AttentionAIPlayer orchestrator.
class AttentionSubAgent {
public:
    virtual ~AttentionSubAgent() = default;

    // Produce attention deltas and optional direct commands.
    // - current_attention: the current attention field (read-only)
    // - deltas_out: additive deltas to be weighted and applied
    // - direct_commands_out: commands that bypass the attention flow
    virtual void contribute(const Game& game, int player_id,
                            const std::vector<float>& current_attention,
                            std::vector<float>& deltas_out,
                            PlayerCommands& direct_commands_out) = 0;

    // Set by AttentionAIPlayer before contribute(). Sub-agents write metrics here if non-null.
    AIMetricsSnapshot* metrics_out = nullptr;
};

#endif
