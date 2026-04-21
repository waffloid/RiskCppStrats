#include "ai/models/model_registry.hpp"
#include "ai/model_config.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/conditional_expansion_agent.hpp"
#include "ai/sub_agents/direct_war_agent.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"

void register_v12() {
    // v12: v11 + frontline troop pooling (OT demands troops near enemy borders)
    register_model("v12", [](int player_id) {
        ModelConfig cfg;
        cfg.expansion_unconstructed_cutoff = 0.2f;
        cfg.ot_frontline_garrison = 50;
        auto ai = std::make_unique<DistributionAIPlayer>(player_id, cfg);
        ai->add_sub_agent(std::make_unique<EconomySubAgent>(cfg,
            [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
                return economy_solver_qubo(g, n, pid, c, "production", "gw");
            }), cfg.economy_pool_weight);
        ai->add_sub_agent(std::make_unique<ConditionalExpansionSubAgent>(cfg), cfg.expansion_pool_weight);
        ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(cfg), cfg.war_pool_weight);
        ai->enable_ot_transport();
        ai->enable_effective_troops();
        return ai;
    });
}
