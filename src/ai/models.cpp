#include "ai/models.hpp"
#include "ai/players/attention_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "ai/sub_agents/knapsack_war_agent.hpp"
#include "ai/sub_agents/direct_war_agent.hpp"
#include "ai/sub_agents/bootstrap_economy_agent.hpp"

#include <map>

static const std::map<std::string, ModelFactory>& registry() {
    static const std::map<std::string, ModelFactory> models = {
        {"v0_expansion", [](int player_id) {
            // Original behavior: Economy + Expansion
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), 1.0f);
            return ai;
        }},
        {"v1_knapsack", [](int player_id) {
            // Knapsack replaces expansion
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<KnapsackWarSubAgent>(), 1.0f);
            return ai;
        }},
        {"v1_knapsack_hybrid", [](int player_id) {
            // Both expansion and knapsack
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), 0.5f);
            ai->add_sub_agent(std::make_unique<KnapsackWarSubAgent>(), 1.0f);
            return ai;
        }},
        {"v2_knapsack", [](int player_id) {
            // v2 direct war: expansion gets troops to the enemy, war fights once there
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(), 1.0f);
            return ai;
        }},
        {"v2_knapsack_hybrid", [](int player_id) {
            // v2 direct war + expansion hybrid (identical to v2_knapsack)
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<EconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(), 1.0f);
            return ai;
        }},
        {"v3_bootstrap", [](int player_id) {
            // Bootstrap economy: planned factory+powerplant build order, then standard eco
            auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
            ai->add_sub_agent(std::make_unique<BootstrapEconomySubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), 1.0f);
            ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(), 1.0f);
            return ai;
        }},
    };
    return models;
}

const ModelFactory* get_model(const std::string& name) {
    const std::map<std::string, ModelFactory>& models = registry();
    auto it = models.find(name);
    if (it == models.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> list_models() {
    std::vector<std::string> names;
    for (const auto& [name, _] : registry()) {
        names.push_back(name);
    }
    return names;
}
