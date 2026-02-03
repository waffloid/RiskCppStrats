#include <cassert>
#include <cstdio>
#include <vector>
#include <random>
#include "engine/edge_lanes.hpp"

// Invariant: if two groups with different owners on opposite lanes have crossed
// (their relative positions indicate they've passed through each other),
// at least one must have forced_retreat == true.
bool check_crossing_invariant(const EdgeLanes& el, std::vector<std::string>& violations) {
    const auto& fwd_groups = el.lanes[0].groups;
    const auto& bwd_groups = el.lanes[1].groups;

    bool ok = true;
    for (size_t fi = 0; fi < fwd_groups.size(); fi++) {
        for (size_t bi = 0; bi < bwd_groups.size(); bi++) {
            const auto& fwd = fwd_groups[fi];
            const auto& bwd = bwd_groups[bi];

            if (fwd.owner == bwd.owner) continue;  // same owner, not relevant

            // Convert to edge frame: fwd in [0,1], bwd at (1 - bwd.position)
            float fwd_pos = fwd.position;
            float bwd_pos = 1.0f - bwd.position;

            // They've crossed if fwd_pos > bwd_pos
            // (fwd started at 0, should be < bwd which started at 1)
            if (fwd_pos > bwd_pos) {
                // They've passed through each other
                if (!fwd.forced_retreat && !bwd.forced_retreat) {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                        "Crossed without retreat: fwd(owner=%d, pos=%.3f, forced=%d) "
                        "bwd(owner=%d, pos=%.3f, forced=%d)",
                        fwd.owner, fwd_pos, fwd.forced_retreat,
                        bwd.owner, bwd_pos, bwd.forced_retreat);
                    violations.push_back(std::string(buf));
                    ok = false;
                }
            }
        }
    }
    return ok;
}

void stress_test_collision_invariant() {
    GameConfig config;
    config.aggregation_buffer = 0.05f;

    std::mt19937 rng(42);  // fixed seed for reproducibility
    std::uniform_int_distribution<> troop_size_dist(20, 200);
    std::uniform_int_distribution<> send_interval_dist(5, 20);
    std::uniform_int_distribution<> direction_dist(0, 1);
    std::uniform_real_distribution<> dt_dist(0.1f, 0.5f);

    auto el = EdgeLanes();
    el.edge_idx = 0;
    el.node_a = 0;
    el.node_b = 1;
    el.edge_length = 10.0f;

    float dt = 0.25f;  // base tick rate (could randomize per tick)
    int num_ticks = 1000;

    int total_troops_inserted = 0;
    int total_troops_arrived = 0;
    int invariant_violations = 0;
    float max_stuck_time = 0.0f;
    int num_sends = 0;

    printf("Randomized collision invariant stress test:\n");
    printf("  Random troop sizes (20-200)\n");
    printf("  Random send intervals (5-20 ticks)\n");
    printf("  Random directions\n");
    printf("  Running %d ticks with dt=%.2f\n\n", num_ticks, dt);

    int ticks_until_next_send = send_interval_dist(rng);

    for (int tick = 0; tick < num_ticks; tick++) {
        // Random sends
        if (ticks_until_next_send <= 0) {
            int size = troop_size_dist(rng);
            int direction = direction_dist(rng);
            if (direction == 0) {
                insert_troop_group(el, 0, 0, size, 1);
            } else {
                insert_troop_group(el, 1, 1, size, 0);
            }
            total_troops_inserted += size;
            num_sends++;
            ticks_until_next_send = send_interval_dist(rng);
        }
        ticks_until_next_send--;

        // Update edge
        std::vector<Arrival> arrivals;
        update_edge(el, dt, config, arrivals);

        // Check invariant
        std::vector<std::string> violations;
        if (!check_crossing_invariant(el, violations)) {
            invariant_violations += violations.size();
            for (const auto& v : violations) {
                printf("Tick %d: %s\n", tick, v.c_str());
            }
        }

        // Track arrivals
        for (const auto& a : arrivals) {
            total_troops_arrived += a.count;
        }

        // Track how long groups stay on edge
        for (const auto& g : el.lanes[0].groups) {
            if (g.time_elapsed > max_stuck_time) max_stuck_time = g.time_elapsed;
        }
        for (const auto& g : el.lanes[1].groups) {
            if (g.time_elapsed > max_stuck_time) max_stuck_time = g.time_elapsed;
        }
    }

    int troops_remaining = 0;
    for (const auto& g : el.lanes[0].groups) troops_remaining += g.count;
    for (const auto& g : el.lanes[1].groups) troops_remaining += g.count;

    printf("\nResults:\n");
    printf("  Number of sends: %d\n", num_sends);
    printf("  Total troops inserted: %d\n", total_troops_inserted);
    printf("  Total troops arrived: %d\n", total_troops_arrived);
    printf("  Troops remaining on edge: %d\n", troops_remaining);
    printf("  Total accounted: %d\n", total_troops_arrived + troops_remaining);
    printf("  Max time stuck on edge: %.1f ticks\n", max_stuck_time);
    printf("  Invariant violations: %d\n", invariant_violations);

    if (invariant_violations > 0) {
        printf("\nFAILED: %d violations detected\n", invariant_violations);
        assert(false);
    }

    printf("PASSED: collision invariant held for all %d ticks\n", num_ticks);
}

int main() {
    stress_test_collision_invariant();
    return 0;
}
