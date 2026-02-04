#ifndef CRISKY_GAME_CONFIG_HPP
#define CRISKY_GAME_CONFIG_HPP

struct GameConfig {
    // Graph generation
    float poisson_intensity = 0.005f;
    float region_width = 100.0f;
    float region_height = 100.0f;
    float edge_distance_threshold = 20.0f;
    int max_neighbors = 6;

    // Circular map with holes
    bool circular = false;
    int num_holes = 0;
    float hole_radius_min = 10.0f;
    float hole_radius_max = 20.0f;

    // Troops
    int init_troop_count = 501;
    float displacement_c2 = 5.0f;

    // Combat
    float attack_divisor = 100.0f;
    float defense_divisor = 1000.0f;
    float fort_defense_mult = 1.2f;
    float artillery_attack_mult = 1.5f;
    float base_dt = 1.0f;  // reference dt for which combat values are tuned

    // Production
    int capital_troops_per_tick = 2;
    int factory_troops_per_tick = 1;
    int powerplant_bonus = 2;

    // Building costs
    int cost_factory = 500;
    int cost_powerplant = 2500;
    int cost_fort = 400;
    int cost_artillery = 4000;

    // Neutral defenders on default nodes (0 = disabled, adds an extra neutral player)
    int init_default_troops = 0;

    // Edge lanes
    float radius_factor = 0.01f;        // world-space: rf * sqrt(count) + buffer
    float aggregation_buffer = 1.0f;     // world-space merge distance; must exceed one tick's displacement
};

#endif
