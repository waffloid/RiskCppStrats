#ifndef CRISKY_WAR_UTILITIES_HPP
#define CRISKY_WAR_UTILITIES_HPP

#include "ai/distribution_sub_agent.hpp"
#include <unordered_set>
#include <vector>

// --- Frontier target types ---

struct FrontierTarget {
    int node_idx;
    float cost;     // effective troop cost to capture
    float value;    // strategic value of capturing
    float ratio;    // value / cost
};

struct V2Target {
    int node_idx;
    float cost;          // effective troop cost to capture
    float value;         // strategic value of capturing
    float priority;      // value / (degree/K + 1), higher = attack first
    std::vector<int> our_neighbors;  // owned nodes adjacent to this target
};

// --- Target evaluation ---

float frontier_target_cost(const Game& game, int node_idx, int player_id);
float frontier_target_value(const Game& game, int node_idx, int player_id);

// --- Frontier extraction ---

// Extract the opposing frontier targets sorted by value/cost ratio (descending).
// budget_out is set to the total troops on our frontier nodes.
std::vector<FrontierTarget> extract_frontier(const Game& game, int player_id,
                                              float& budget_out);

// --- Knapsack solvers ---

std::vector<int> knapsack_greedy(const std::vector<FrontierTarget>& targets,
                                  float budget);
std::vector<int> knapsack_optimal(const std::vector<FrontierTarget>& targets,
                                   float budget);
float selection_value(const std::vector<FrontierTarget>& targets,
                      const std::vector<int>& selected);
float selection_cost(const std::vector<FrontierTarget>& targets,
                     const std::vector<int>& selected);

// --- V2 per-node budget solver ---

float v2_target_priority(float value, int degree, int K);

std::vector<TroopCommand> v2_solve_attacks(
    const Game& game, int player_id,
    const std::vector<V2Target>& targets,
    std::vector<int>& available);

// --- Shared war context ---
// Built once per tick by build_war_context, consumed by both war agents.

struct WarContext {
    std::unordered_set<int> our_nodes;
    std::vector<int> available;       // per-node available troops (troops - 1 garrison)
    std::vector<V2Target> targets;    // enemy frontier targets, sorted by priority desc
};

// Build territory info, enemy frontier targets (filtered to real live enemies),
// and available troop counts. Targets are sorted by priority descending.
WarContext build_war_context(const Game& game, int player_id);

#endif
