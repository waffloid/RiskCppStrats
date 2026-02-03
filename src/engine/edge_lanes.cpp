#include "edge_lanes.hpp"
#include <algorithm>
#include <cmath>

float compute_displacement(int count, float dt, float edge_length, const GameConfig& config) {
    float raw = (config.displacement_c2 / std::cbrt(static_cast<float>(count))) * dt;
    float clamped = std::min(raw, dt);
    // Convert from world-space displacement to position-space (fraction of edge)
    return clamped / edge_length;
}

static float aggregation_radius(int count, const GameConfig& config) {
    return config.radius_factor * std::sqrt(static_cast<float>(count)) + config.aggregation_buffer;
}

void insert_troop_group(EdgeLanes& el, int origin_node, int owner, int count,
                        int global_dest_node) {
    int lane_idx = (origin_node == el.node_a) ? 0 : 1;
    auto& groups = el.lanes[lane_idx].groups;

    TroopGroup g;
    g.owner = owner;
    g.count = count;
    g.position = 0.0f;  // starts at origin end
    g.global_dest_node = global_dest_node;
    g.forced_retreat = false;
    g.retreating = false;
    g.time_elapsed = 0.0f;

    // Insert sorted by position (new group at 0.0 goes to front)
    auto it = std::lower_bound(groups.begin(), groups.end(), g,
        [](const TroopGroup& a, const TroopGroup& b) {
            return a.position < b.position;
        });
    groups.insert(it, g);
}

// Advance all groups in a lane
static void advance_lane(Lane& lane, float dt, float edge_length, const GameConfig& config) {
    for (auto& g : lane.groups) {
        float disp = compute_displacement(g.count, dt, edge_length, config);
        if (g.retreating) {
            g.position -= disp;
            g.time_elapsed += dt;
        } else {
            g.position += disp;
            g.time_elapsed += dt;
        }
    }
}

// Aggregate same-direction, same-owner groups that are close enough.
// Groups are sorted by position ascending (toward destination).
// Smaller trailing groups are faster and catch larger leading groups.
static void aggregate_lane(Lane& lane, const GameConfig& config, float edge_length) {
    auto& groups = lane.groups;
    if (groups.size() < 2) return;

    // Groups sorted by position ascending: index 0 = near origin, last = near dest.
    // Sweep from back toward front, checking adjacent pairs.
    // groups[write] is leading (ahead, higher position), groups[i] is trailing (behind).
    int write = static_cast<int>(groups.size()) - 1;
    for (int i = static_cast<int>(groups.size()) - 2; i >= 0; i--) {
        auto& trailing = groups[i];
        auto& leading = groups[write];

        // Only same-owner, same-direction groups aggregate
        if (trailing.owner != leading.owner ||
            trailing.retreating != leading.retreating) {
            write = i;
            continue;
        }

        // Check distance (in position-space)
        float dist = leading.position - trailing.position;
        int max_count = std::max(leading.count, trailing.count);
        float threshold = (config.radius_factor * std::sqrt(static_cast<float>(max_count))
                          + config.aggregation_buffer) / edge_length;

        if (dist <= threshold) {
            // Merge trailing into leading (leading is ahead, keeps position)
            leading.count += trailing.count;
            trailing.count = 0; // mark for removal
            // Don't advance write — the merged leading group may now
            // aggregate with the next trailing group too.
        } else {
            write = i;
        }
    }

    // Remove merged groups
    groups.erase(
        std::remove_if(groups.begin(), groups.end(),
            [](const TroopGroup& g) { return g.count == 0; }),
        groups.end());
}

// Resolve collisions between opposing lanes.
// lane_fwd groups move toward position 1.0, lane_bwd groups move toward position 1.0
// (in their own frame). We need to compare in a common frame.
// In the common frame of the edge (0 = node_a, 1 = node_b):
//   lane[0] groups have position in [0, 1], moving toward 1
//   lane[1] groups have position in [0, 1], but they move from node_b (pos 0 in their frame = pos 1 in edge frame)
// So lane[1] group at position p in its frame is at position (1 - p) in edge frame.
static void resolve_collisions(EdgeLanes& el, const GameConfig& config) {
    Lane& fwd = el.lanes[0];
    Lane& bwd = el.lanes[1];
    if (fwd.groups.empty() || bwd.groups.empty()) return;

    // Check all pairs of (fwd, bwd) groups for collisions.
    // A collision is detected if they've crossed: fwd_pos > bwd_pos in edge frame.
    // fwd groups: position in [0, 1], moving forward
    // bwd groups: position in [0, 1] in lane frame, which is (1 - position) in edge frame
    for (auto& fwd_group : fwd.groups) {
        for (auto& bwd_group : bwd.groups) {
            if (fwd_group.forced_retreat || bwd_group.forced_retreat) {
                // One side already forced, skip pair
                continue;
            }

            float fwd_pos = fwd_group.position;        // in edge frame [0, 1]
            float bwd_pos = 1.0f - bwd_group.position; // in edge frame [0, 1]

            // They've crossed if fwd_pos > bwd_pos (fwd started at 0, bwd at 1)
            if (fwd_pos > bwd_pos) {
                // Determine winner by count
                if (fwd_group.count > bwd_group.count) {
                    bwd_group.forced_retreat = true;
                    bwd_group.retreating = true;
                } else if (bwd_group.count > fwd_group.count) {
                    fwd_group.forced_retreat = true;
                    fwd_group.retreating = true;
                } else {
                    // Equal counts: deterministic tie-breaker using edge node IDs
                    bool fwd_retreats = (el.node_a + el.node_b) % 2 == 0;
                    if (fwd_retreats) {
                        fwd_group.forced_retreat = true;
                        fwd_group.retreating = true;
                    } else {
                        bwd_group.forced_retreat = true;
                        bwd_group.retreating = true;
                    }
                }
            }
        }
    }
}

// Extract groups that have arrived at their destination (or retreated to origin)
static void extract_arrivals(Lane& lane, int dest_node, int origin_node,
                             std::vector<Arrival>& arrivals_out) {
    auto& groups = lane.groups;

    groups.erase(
        std::remove_if(groups.begin(), groups.end(),
            [&](const TroopGroup& g) {
                if (g.retreating && g.position <= 0.0f) {
                    // Retreated back to origin — deposit there, clear global routing
                    Arrival a;
                    a.arrived_at_node = origin_node;
                    a.owner = g.owner;
                    a.count = g.count;
                    a.global_dest_node = -1;  // forget routing
                    arrivals_out.push_back(a);
                    return true;
                }
                if (!g.retreating && g.position >= 1.0f) {
                    // Arrived at destination
                    Arrival a;
                    a.arrived_at_node = dest_node;
                    a.owner = g.owner;
                    a.count = g.count;
                    a.global_dest_node = g.global_dest_node;
                    arrivals_out.push_back(a);
                    return true;
                }
                return false;
            }),
        groups.end());
}

void update_edge(EdgeLanes& el, float dt, const GameConfig& config,
                 std::vector<Arrival>& arrivals_out) {
    // 1. Advance positions
    advance_lane(el.lanes[0], dt, el.edge_length, config);
    advance_lane(el.lanes[1], dt, el.edge_length, config);

    // 2. Aggregate same-direction groups
    aggregate_lane(el.lanes[0], config, el.edge_length);
    aggregate_lane(el.lanes[1], config, el.edge_length);

    // 3. Resolve opposite-direction collisions
    resolve_collisions(el, config);

    // 4. Extract arrivals
    // lane[0] = a->b: dest is node_b, origin is node_a
    // lane[1] = b->a: dest is node_a, origin is node_b
    extract_arrivals(el.lanes[0], el.node_b, el.node_a, arrivals_out);
    extract_arrivals(el.lanes[1], el.node_a, el.node_b, arrivals_out);
}
