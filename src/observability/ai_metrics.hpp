#ifndef CRISKY_AI_METRICS_HPP
#define CRISKY_AI_METRICS_HPP

#include <string>
#include <vector>

// Snapshot of one sub-agent's output (pre- and post-softmax).
struct SubAgentSnapshot {
    std::string name;
    float weight = 0.0f;
    float beta = 1.0f;
    std::vector<float> raw_scores;     // pre-softmax
    std::vector<float> distribution;   // post-softmax
};

// Full snapshot of the distribution pipeline for one tick.
// Populated by DistributionAIPlayer when metrics_enabled_.
struct AIDecisionSnapshot {
    std::vector<SubAgentSnapshot> sub_agents;
    std::vector<float> pooled;         // weighted pool (pre-EMA)
    std::vector<float> smoothed;       // post-EMA (final distribution)
    std::vector<float> gradient;       // deficit signal
    std::vector<int> current_troops;   // per-node troop count for this player
    int total_owned_troops = 0;
};

// Per-tick snapshot of AI decision quality for one player.
// Populated by DistributionAIPlayer and its sub-agents during decide().
struct AIMetricsSnapshot {
    int tick = 0;
    int player_id = -1;

    // Knapsack approximation ratio: greedy / optimal (1.0 = perfect).
    // Only meaningful when n_targets <= 20 (exact solve is feasible).
    float knapsack_greedy_value  = 0.0f;
    float knapsack_optimal_value = 0.0f;
    float knapsack_ratio         = 1.0f;
    int   knapsack_n_targets     = 0;
    float knapsack_budget        = 0.0f;

    // V2 per-node solver efficiency.
    float v2_value_captured  = 0.0f;   // sum of value of distinct targets attacked
    float v2_troops_spent    = 0.0f;   // total troops committed
    float v2_efficiency      = 0.0f;   // value / troops (0 if none spent)
    int   v2_targets_attacked = 0;

    // Distribution field quality.
    float distribution_entropy       = 0.0f; // Shannon entropy of troop distribution
    float distribution_gradient_util = 0.0f; // fraction of positive-gradient nodes with inflow
    float distribution_concentration = 0.0f; // Herfindahl index (sum of squared weights)
};

// Per-tick game-level metrics for one player (computed externally by MetricsCollector).
struct GameMetricsSnapshot {
    int tick = 0;
    int player_id = -1;

    // Combat (exact, from engine kill counters).
    int   kills_this_tick  = 0;
    int   deaths_this_tick = 0;
    int   cumulative_kills  = 0;
    int   cumulative_deaths = 0;
    float kd_ratio          = 0.0f;

    // Economy.
    int   total_troops              = 0;
    int   production_per_tick       = 0;  // actual production rate
    int   theoretical_max_production = 0; // if every factory were powered
    float economy_efficiency        = 0.0f;
    int   factories_owned   = 0;
    int   factories_powered = 0;
    int   powerplants_owned = 0;

    // Territory.
    int   nodes_owned         = 0;
    int   total_nodes         = 0;
    float territory_fraction  = 0.0f;
    int   frontier_perimeter  = 0;  // edges between our nodes and non-our nodes
    float perimeter_ratio     = 0.0f; // frontier_perimeter / nodes_owned (compactness)
    int   expansion_rate      = 0;  // nodes_owned delta from previous tick
};

#endif
