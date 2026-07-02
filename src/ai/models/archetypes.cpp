#include "ai/models/model_registry.hpp"
#include "ai/model_config.hpp"
#include "ai/players/distribution_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/conditional_expansion_agent.hpp"
#include "ai/sub_agents/direct_war_agent.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"

// Strategic-posture archetypes on a shared chassis (v12's QUBO economy + OT
// transport), for testing whether posture alone induces a rock-paper-scissors
// cycle. Only the posture knobs differ:
//   rock     -- aggressive: war heavily weighted, no border garrison
//   paper    -- defensive, distrustful: large frontline garrison, timid war
//   scissors -- defensive, trusting: no war agent at all, no garrison
namespace {

std::unique_ptr<DistributionAIPlayer> make_chassis(int player_id, const ModelConfig& cfg,
                                                   bool with_war) {
    auto ai = std::make_unique<DistributionAIPlayer>(player_id, cfg);
    ai->add_sub_agent(std::make_unique<EconomySubAgent>(cfg,
        [](const Graph& g, const std::vector<NodeData>& n, int pid, const GameConfig& c) {
            return economy_solver_qubo(g, n, pid, c, "production", "gw");
        }), cfg.economy_pool_weight);
    ai->add_sub_agent(std::make_unique<ConditionalExpansionSubAgent>(cfg), cfg.expansion_pool_weight);
    if (with_war) {
        ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(cfg), cfg.war_pool_weight);
    }
    ai->enable_ot_transport();
    ai->enable_effective_troops();
    return ai;
}

} // namespace

void register_archetypes() {
    register_model("arch_rock", [](int player_id) {
        ModelConfig cfg;
        cfg.expansion_unconstructed_cutoff = 0.2f;
        cfg.ot_frontline_garrison = 0;   // no border stacking
        cfg.war_pool_weight = 5.0f;      // war on par with economy
        cfg.war_front_line_score = 6.0f; // reserves pulled hard to the front
        return make_chassis(player_id, cfg, /*with_war=*/true);
    });

    register_model("arch_paper", [](int player_id) {
        ModelConfig cfg;
        cfg.expansion_unconstructed_cutoff = 0.2f;
        cfg.ot_frontline_garrison = 150; // heavy border stacking (v12 uses 50)
        cfg.war_pool_weight = 0.25f;     // war agent kept only for retreats/openings
        return make_chassis(player_id, cfg, /*with_war=*/true);
    });

    register_model("arch_scissors", [](int player_id) {
        ModelConfig cfg;
        cfg.expansion_unconstructed_cutoff = 0.2f;
        cfg.ot_frontline_garrison = 0;   // trusts: no garrison at all
        return make_chassis(player_id, cfg, /*with_war=*/false);
    });
}
