#ifndef CRISKY_ECONOMY_AGENT_HPP
#define CRISKY_ECONOMY_AGENT_HPP

#include "ai/distribution_sub_agent.hpp"
#include "ai/model_config.hpp"

// Economy sub-agent: demand-based scoring for factory/powerplant placement.
//
// Scoring:
//   - Owned DEFAULT nodes get factory_score (attract troops for factory building)
//   - Owned DEFAULT nodes with >= min_pp_neighbors adjacent factories/capitals
//     get powerplant_score (higher, attracts troops faster for PP building)
//
// Building is parasitic: when troops accumulate beyond cost, build immediately.
// Natural progression: factories spread first (cheap), then PP sites emerge
// at cluster centers (5+ factory neighbors = +10 production from one PP).
class EconomySubAgent : public DistributionSubAgent {
public:
    explicit EconomySubAgent(const ModelConfig& cfg) : config_(cfg) {}

    const char* name() const override { return "economy"; }

    void score(const Game& game, int player_id,
               std::vector<float>& scores_out,
               PlayerCommands& direct_commands_out) override;

    float beta() const override { return config_.economy_beta; }

private:
    ModelConfig config_;
};

#endif
