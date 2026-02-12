#ifndef CRISKY_MODEL_CONFIG_HPP
#define CRISKY_MODEL_CONFIG_HPP

// All tunable AI parameters in one struct.
// Default values match the original hardcoded constants (behavior-preserving).
// GA tuning mutates this struct directly.

struct ModelConfig {
    // Economy sub-agent
    float economy_factory_score    = 1.0f;   // score for DEFAULT nodes (factory candidates)
    float economy_powerplant_score = 5.0f;   // score for nodes with enough factory neighbors
    int   economy_min_pp_neighbors = 5;      // min adjacent factories/capitals for PP demand
    float economy_beta             = 1.0f;

    // Expansion sub-agent
    float expansion_border_score = 5.0f;
    float expansion_beta         = 1.0f;

    // War sub-agent
    float war_front_line_score  = 5.0f;
    float war_capital_value     = 5.0f;
    float war_factory_value     = 1.0f;
    float war_powerplant_value  = 2.0f;
    float war_artillery_value   = 3.0f;
    float war_beta              = 2.0f;

    // Distribution pipeline
    float ema_alpha = 0.3f;

    // Pool weights
    float economy_pool_weight   = 1.0f;
    float expansion_pool_weight = 1.0f;
    float war_pool_weight       = 1.0f;

    // Transport
    float transport_outflow_rate = 0.1f;
    int   transport_min_troops   = 1;
    int   transport_min_send     = 5;
};

#endif
