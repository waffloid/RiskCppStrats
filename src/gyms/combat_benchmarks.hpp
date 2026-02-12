#ifndef CRISKY_COMBAT_BENCHMARKS_HPP
#define CRISKY_COMBAT_BENCHMARKS_HPP

#include "engine/graph.hpp"
#include "engine/game_config.hpp"
#include "engine/benchmark.hpp"  // NodeOverride

#include <optional>
#include <string>
#include <vector>

struct CombatBenchmark {
    std::string name;
    Graph graph;
    std::vector<int> capitals;           // capitals[0] = test player, [1] = opponent
    std::vector<NodeOverride> overrides;
    GameConfig config;
    int max_ticks = 2000;
};

// Get a named benchmark. Returns nullopt if name not found.
std::optional<CombatBenchmark> get_combat_benchmark(const std::string& name);

// List all available benchmark names.
std::vector<std::string> list_combat_benchmarks();

#endif
