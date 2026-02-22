#ifndef CRISKY_MODEL_CONFIG_HPP
#define CRISKY_MODEL_CONFIG_HPP

// All tunable AI parameters in one struct.
// GA tuning mutates this struct directly.

struct ModelConfig {
    // Distribution pipeline
    float global_beta = 3.0f;    // softmax temperature on combined scores (higher = more concentrated)
    float ema_alpha   = 0.15f;   // EMA smoothing (low = stable targets, high = responsive)

    // Economy sub-agent
    float economy_factory_score    = 3.0f;   // base score for factory candidates (scaled by neighbor quality)
    float economy_powerplant_score = 8.0f;   // score for powerplant targets in build queue

    // Expansion sub-agent
    float expansion_border_score = 1.0f;     // score for border nodes (lower than economy to build first)
    float expansion_unconstructed_cutoff = 1.0f;  // disable expansion if fraction of owned nodes unconstructed exceeds this (1.0 = never disable)

    // War sub-agent (attacks via direct commands; distribution scores pull reserves forward)
    float war_front_line_score  = 2.0f;
    float war_capital_value     = 5.0f;
    float war_factory_value     = 1.0f;
    float war_powerplant_value  = 2.0f;
    float war_artillery_value   = 3.0f;

    // Pool weights (linear combination before softmax)
    float economy_pool_weight   = 5.0f;
    float expansion_pool_weight = 1.0f;
    float war_pool_weight       = 1.0f;

    // Distribution mode
    bool use_distance_softmax = false;  // use distance-decayed local softmax instead of global

    // Transport
    float transport_outflow_rate = 0.15f;
    int   transport_min_troops   = 1;

    // OT transport (Frank-Wolfe convex saturation)
    float ot_saturation_alpha    = 0.005f;  // quadratic penalty on future production
    float ot_value_alpha         = 0.0f;    // demand→SINK negative-cost bonus scale
    int   ot_fw_iterations       = 8;       // Frank-Wolfe iterations per tick
};

#endif
