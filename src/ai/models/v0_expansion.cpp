#include "ai/models/model_registry.hpp"
#include "ai/model_config.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"

void register_v0_expansion() {
    register_model("v0_expansion", [](int player_id) {
        ModelConfig cfg;
        auto ai = std::make_unique<DistributionAIPlayer>(player_id, cfg);
        ai->add_sub_agent(std::make_unique<EconomySubAgent>(cfg), cfg.economy_pool_weight);
        ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(cfg), cfg.expansion_pool_weight);
        return ai;
    });
}
