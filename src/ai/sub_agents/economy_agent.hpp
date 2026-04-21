#ifndef CRISKY_ECONOMY_AGENT_HPP
#define CRISKY_ECONOMY_AGENT_HPP

#include <functional>
#include <vector>
#include "ai/distribution_sub_agent.hpp"
#include "ai/model_config.hpp"
#include "engine/game_config.hpp"
#include "engine/game_state.hpp"
#include "engine/graph.hpp"
#include "systems/common/types.hpp"

// Economy sub-agent with solver-derived persistent build queue.
//
// Runs the economy solver once on first tick to compute the ideal layout
// for the full map, caches it forever. Each tick, rebuilds a short queue
// from the cached plan by filtering to owned+unbuilt+feasible targets
// ordered by proximity to existing infrastructure.
//
// Default solver: MCMC. Pass a custom EconomySolver to override.
class EconomySubAgent : public DistributionSubAgent {
public:
    using EconomySolver = std::function<BuildPlan(
        const Graph&, const std::vector<NodeData>&, int, const GameConfig&)>;

    explicit EconomySubAgent(const ModelConfig& cfg) : config_(cfg) {}
    EconomySubAgent(const ModelConfig& cfg, EconomySolver solver)
        : config_(cfg), solver_(std::move(solver)) {}

    const char* name() const override { return "economy"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    const std::vector<BuildCommand>& build_queue() const { return queue_; }

private:
    ModelConfig config_;
    EconomySolver solver_;                  // optional override (null = MCMC)
    std::vector<BuildCommand> ideal_plan_;  // cached solver result (computed once)
    std::vector<BuildCommand> queue_;       // filtered+ordered build queue
    bool plan_computed_ = false;

    void compute_plan(const Game& game, int player_id);
    void rebuild_queue(const Game& game, int player_id);
};

#endif
