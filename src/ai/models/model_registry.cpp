#include "ai/models/model_registry.hpp"
#include <map>

static std::map<std::string, ModelFactory>& mutable_registry() {
    static std::map<std::string, ModelFactory> models;
    return models;
}

void register_model(const char* name, ModelFactory factory) {
    mutable_registry()[name] = std::move(factory);
}

// Forward declarations of per-model registration functions.
extern void register_v0_expansion();
extern void register_v1_knapsack();
extern void register_v1_knapsack_hybrid();
extern void register_v2_knapsack();
extern void register_v3();
extern void register_v4();

static void ensure_registered() {
    static bool done = false;
    if (done) return;
    done = true;
    register_v0_expansion();
    register_v1_knapsack();
    register_v1_knapsack_hybrid();
    register_v2_knapsack();
    register_v3();
    register_v4();
}

const ModelFactory* get_model(const std::string& name) {
    ensure_registered();
    auto& models = mutable_registry();
    auto it = models.find(name);
    if (it == models.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> list_models() {
    ensure_registered();
    std::vector<std::string> names;
    for (const auto& [name, _] : mutable_registry()) {
        names.push_back(name);
    }
    return names;
}
