#include "gyms/combat_benchmarks.hpp"
#include "engine/graph_builder.hpp"

#include <functional>
#include <map>

// --- Helpers ---

static Graph build_grid(int rows, int cols, float spacing = 15.0f) {
    std::vector<std::pair<float, float>> positions;
    std::vector<std::pair<int, int>> edges;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            positions.push_back({c * spacing, r * spacing});
        }
    }

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if (c + 1 < cols) edges.push_back({idx, idx + 1});
            if (r + 1 < rows) edges.push_back({idx, idx + cols});
        }
    }

    return build_graph(positions, edges);
}

// --- Benchmark definitions ---

static CombatBenchmark make_corridor() {
    CombatBenchmark b;
    b.name = "corridor";
    b.graph = build_path(10);
    b.capitals = {0, 9};
    b.config.init_troop_count = 200;
    b.max_ticks = 3000;
    return b;
}

static CombatBenchmark make_bipartite_3_5() {
    CombatBenchmark b;
    b.name = "bipartite_3_5";
    b.graph = build_bipartite(3, 5);
    b.capitals = {0, 3};
    b.config.init_troop_count = 300;
    b.max_ticks = 2000;
    // Each player gets territory in their partition
    b.overrides.push_back({1, NodeState::DEFAULT, 0, 150});
    b.overrides.push_back({2, NodeState::DEFAULT, 0, 150});
    b.overrides.push_back({4, NodeState::DEFAULT, 1, 150});
    b.overrides.push_back({5, NodeState::DEFAULT, 1, 150});
    return b;
}

static CombatBenchmark make_star_hub() {
    CombatBenchmark b;
    b.name = "star_hub";
    b.graph = build_star(6);
    b.capitals = {0, 1};  // player 0 at center (advantage), player 1 at leaf 1
    b.config.init_troop_count = 300;
    b.max_ticks = 2000;
    // Give opponent some leaves
    b.overrides.push_back({2, NodeState::DEFAULT, 1, 100});
    b.overrides.push_back({3, NodeState::DEFAULT, 1, 100});
    return b;
}

static CombatBenchmark make_grid_5x5() {
    CombatBenchmark b;
    b.name = "grid_5x5";
    b.graph = build_grid(5, 5);
    b.capitals = {0, 24};  // opposite corners
    b.config.init_troop_count = 200;
    b.max_ticks = 3000;

    // Diagonal split: each player owns their corner triangle.
    // P0 = top-left triangle, P1 = bottom-right triangle, center contested.
    //   0* 1  2  .  .
    //   5  6  .  .  .
    //  10  .  .  .  .
    //   .  .  . 18 19
    //   .  .  . 23 24*
    for (int node : {1, 2, 5, 6, 10})
        b.overrides.push_back({node, NodeState::DEFAULT, 0, 150});
    for (int node : {14, 18, 19, 22, 23})
        b.overrides.push_back({node, NodeState::DEFAULT, 1, 150});

    return b;
}

// --- Registry ---

using BenchmarkFactory = std::function<CombatBenchmark()>;

static const std::map<std::string, BenchmarkFactory>& benchmark_registry() {
    static const std::map<std::string, BenchmarkFactory> benchmarks = {
        {"corridor",       make_corridor},
        {"bipartite_3_5",  make_bipartite_3_5},
        {"star_hub",       make_star_hub},
        {"grid_5x5",       make_grid_5x5},
    };
    return benchmarks;
}

std::optional<CombatBenchmark> get_combat_benchmark(const std::string& name) {
    const auto& reg = benchmark_registry();
    auto it = reg.find(name);
    if (it == reg.end()) return std::nullopt;
    return it->second();
}

std::vector<std::string> list_combat_benchmarks() {
    std::vector<std::string> names;
    for (const auto& [name, _] : benchmark_registry()) {
        names.push_back(name);
    }
    return names;
}
