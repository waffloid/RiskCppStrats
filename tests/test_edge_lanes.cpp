#include <cassert>
#include <cstdio>
#include <cmath>
#include "engine/edge_lanes.hpp"

static EdgeLanes make_edge(int a, int b, float length) {
    EdgeLanes el;
    el.edge_idx = 0;
    el.node_a = a;
    el.node_b = b;
    el.edge_length = length;
    return el;
}

void test_basic_movement() {
    GameConfig config;
    auto el = make_edge(0, 1, 10.0f);

    // Insert 100 troops from node 0 heading to node 1
    insert_troop_group(el, 0, /*owner=*/0, /*count=*/100, /*global_dest=*/1);
    assert(el.lanes[0].groups.size() == 1);
    assert(el.lanes[0].groups[0].position == 0.0f);

    // Tick
    std::vector<Arrival> arrivals;
    update_edge(el, 1.0f, config, arrivals);

    // Should have moved forward
    assert(el.lanes[0].groups.size() == 1);
    float pos = el.lanes[0].groups[0].position;
    assert(pos > 0.0f);

    printf("test_basic_movement passed (pos after 1 tick: %f)\n", pos);
}

void test_arrival() {
    GameConfig config;
    auto el = make_edge(0, 1, 1.0f); // very short edge

    // 1 troop moves at max speed (dt per tick)
    insert_troop_group(el, 0, 0, 1, 1);

    // Tick many times until arrival
    std::vector<Arrival> arrivals;
    for (int i = 0; i < 100; i++) {
        update_edge(el, 1.0f, config, arrivals);
        if (!arrivals.empty()) break;
    }

    assert(!arrivals.empty());
    assert(arrivals[0].arrived_at_node == 1);
    assert(arrivals[0].owner == 0);
    assert(arrivals[0].count == 1);
    assert(arrivals[0].global_dest_node == 1);
    assert(el.lanes[0].groups.empty());

    printf("test_arrival passed\n");
}

void test_collision_bigger_wins() {
    GameConfig config;
    auto el = make_edge(0, 1, 10.0f);

    // 200 troops from node 0 (lane 0, a->b)
    insert_troop_group(el, 0, 0, 200, 1);
    // 50 troops from node 1 (lane 1, b->a)
    insert_troop_group(el, 1, 1, 50, 0);

    // Move them close enough to collide. Place them manually.
    el.lanes[0].groups[0].position = 0.6f;
    el.lanes[1].groups[0].position = 0.6f;  // in lane[1] frame, 0.6 = 0.4 in edge frame

    // They overlap: fwd at 0.6, bwd at 1.0 - 0.6 = 0.4. fwd > bwd, so they've crossed.
    std::vector<Arrival> arrivals;
    update_edge(el, 0.001f, config, arrivals); // tiny dt so positions barely change

    // The smaller group (50 troops, lane 1) should be retreating
    assert(el.lanes[1].groups[0].forced_retreat == true);
    assert(el.lanes[1].groups[0].retreating == true);
    // The bigger group should NOT be retreating
    assert(el.lanes[0].groups[0].retreating == false);

    printf("test_collision_bigger_wins passed\n");
}

void test_forced_retreat_flag() {
    GameConfig config;
    auto el = make_edge(0, 1, 10.0f);

    // Two groups going opposite directions, same size = stalemate
    insert_troop_group(el, 0, 0, 100, 1);
    insert_troop_group(el, 1, 1, 100, 0);

    // Place them overlapping
    el.lanes[0].groups[0].position = 0.6f;
    el.lanes[1].groups[0].position = 0.6f;

    std::vector<Arrival> arrivals;
    update_edge(el, 0.001f, config, arrivals);

    // Equal counts = stalemate, neither forced
    assert(el.lanes[0].groups[0].forced_retreat == false);
    assert(el.lanes[1].groups[0].forced_retreat == false);

    printf("test_forced_retreat_flag (stalemate) passed\n");
}

void test_aggregation() {
    GameConfig config;
    config.aggregation_buffer = 5.0f; // generous buffer for testing
    auto el = make_edge(0, 1, 10.0f);

    // Insert a big slow group, then a small fast group behind it, same owner
    insert_troop_group(el, 0, 0, 500, 1);
    insert_troop_group(el, 0, 0, 10, 1);

    // Place them close together
    el.lanes[0].groups[0].position = 0.1f;  // small group (sorted first, lower position)
    el.lanes[0].groups[1].position = 0.3f;  // big group (ahead)

    std::vector<Arrival> arrivals;
    update_edge(el, 0.001f, config, arrivals);

    // They should aggregate (small trailing catches big leading)
    assert(el.lanes[0].groups.size() == 1);
    assert(el.lanes[0].groups[0].count == 510);

    printf("test_aggregation passed\n");
}

void test_no_aggregation_different_owners() {
    GameConfig config;
    config.aggregation_buffer = 5.0f;
    auto el = make_edge(0, 1, 10.0f);

    // Two groups, different owners, close together
    insert_troop_group(el, 0, 0, 500, 1);
    insert_troop_group(el, 0, 1, 10, 1);

    el.lanes[0].groups[0].position = 0.1f;
    el.lanes[0].groups[1].position = 0.3f;

    std::vector<Arrival> arrivals;
    update_edge(el, 0.001f, config, arrivals);

    // Should NOT aggregate — different owners
    assert(el.lanes[0].groups.size() == 2);

    printf("test_no_aggregation_different_owners passed\n");
}

void test_retreat_returns_to_origin() {
    GameConfig config;
    auto el = make_edge(0, 1, 10.0f);

    insert_troop_group(el, 0, 0, 50, 1);
    // Advance partway
    el.lanes[0].groups[0].position = 0.3f;
    el.lanes[0].groups[0].time_elapsed = 5.0f;

    // Force retreat
    el.lanes[0].groups[0].retreating = true;

    // Tick until it returns
    std::vector<Arrival> arrivals;
    for (int i = 0; i < 200; i++) {
        update_edge(el, 1.0f, config, arrivals);
        if (!arrivals.empty()) break;
    }

    assert(!arrivals.empty());
    assert(arrivals[0].arrived_at_node == 0);   // back to origin (node_a)
    assert(arrivals[0].global_dest_node == -1); // routing cleared

    printf("test_retreat_returns_to_origin passed\n");
}

// Simulate the real scenario: an AI sends ~50 troops every tick at small dt.
// After many ticks, the number of groups on the edge must stay bounded,
// not grow linearly with tick count.
bool test_repeated_sends_aggregate() {
    GameConfig config;  // default config: radius_factor=0.001, aggregation_buffer=0.077
    float edge_length = 15.0f;  // typical edge length
    auto el = make_edge(0, 1, edge_length);

    float dt = 0.25f;  // slow game speed
    int send_amount = 50;
    int num_ticks = 100;

    std::vector<Arrival> arrivals;
    for (int t = 0; t < num_ticks; t++) {
        // AI sends troops every tick
        insert_troop_group(el, 0, /*owner=*/0, send_amount, /*global_dest=*/1);

        // Advance + aggregate
        arrivals.clear();
        update_edge(el, dt, config, arrivals);
    }

    int group_count = static_cast<int>(el.lanes[0].groups.size());
    // With proper aggregation, groups near the origin should merge.
    // A reasonable bound: edge_length / aggregation_buffer ~ 200, but in practice
    // groups spread out and arrive, so far fewer should exist. Certainly not 100.
    // Be generous: at most 10 groups on a single lane.
    printf("test_repeated_sends_aggregate: %d groups after %d ticks (want <= 10)\n",
           group_count, num_ticks);
    if (group_count > 10) {
        printf("FAILED: too many groups\n");
        return false;
    }
    printf("test_repeated_sends_aggregate passed\n");
    return true;
}

// Same test at normal speed (dt=1.0) — should also stay bounded.
bool test_repeated_sends_aggregate_normal_speed() {
    GameConfig config;
    float edge_length = 15.0f;
    auto el = make_edge(0, 1, edge_length);

    float dt = 1.0f;
    int send_amount = 50;
    int num_ticks = 100;

    std::vector<Arrival> arrivals;
    for (int t = 0; t < num_ticks; t++) {
        insert_troop_group(el, 0, 0, send_amount, 1);
        arrivals.clear();
        update_edge(el, dt, config, arrivals);
    }

    int group_count = static_cast<int>(el.lanes[0].groups.size());
    printf("test_repeated_sends_aggregate_normal_speed: %d groups after %d ticks (want <= 10)\n",
           group_count, num_ticks);
    if (group_count > 10) {
        printf("FAILED: too many groups\n");
        return false;
    }
    printf("test_repeated_sends_aggregate_normal_speed passed\n");
    return true;
}

int main() {
    test_basic_movement();
    test_arrival();
    test_collision_bigger_wins();
    test_forced_retreat_flag();
    test_aggregation();
    test_no_aggregation_different_owners();
    test_retreat_returns_to_origin();
    int fails = 0;
    if (!test_repeated_sends_aggregate()) fails++;
    if (!test_repeated_sends_aggregate_normal_speed()) fails++;
    if (fails > 0) {
        printf("%d aggregation tests FAILED\n", fails);
        return 1;
    }
    printf("All edge lane tests passed\n");
    return 0;
}
