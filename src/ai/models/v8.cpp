#include "ai/models/model_registry.hpp"
#include "ai/model_config.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "ai/sub_agents/direct_war_agent.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"

void register_v8() {
    // v8: v6 + distance-decayed local softmax
    register_model("v8", [](int player_id) {
        ModelConfig cfg;
        cfg.use_distance_softmax = true;
        auto ai = std::make_unique<DistributionAIPlayer>(player_id, cfg);
        ai->add_sub_agent(std::make_unique<EconomySubAgent>(cfg,
            [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
                return economy_solver_qubo(g, n, pid, c, "factory_biased");
            }), cfg.economy_pool_weight);
        ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(cfg), cfg.expansion_pool_weight);
        ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(cfg), cfg.war_pool_weight);
        return ai;
    });
}
