#include "edge_lanes.hpp"
#include <algorithm>
#include <cmath>

float compute_displacement(int count, float dt, float edge_length, const GameConfig& config) {
    float raw = (config.displacement_c1 + config.displacement_c2 / static_cast<float>(count)) * dt;
    float clamped = std::min(raw, dt);
    // Convert from world-space displacement to position-space (fraction of edge)
    return clamped / edge_length;
}

static float aggregation_radius(int count, const GameConfig& config) {
    return (config.radius_factor * static_cast<float>(count) + config.aggregation_buffer)
           / 1.0f; // already in position-space if edge_length normalization needed, handled by caller
}

void insert_troop_group(EdgeLanes& el, int origin_node, int owner, int count,
                        int global_dest_node) {
    int lane_idx = (origin_node == el.node_a) ? 0 : 1;
    TroopGroup g;
    g.owner = owner;
    g.count = count;
    g.position = 0.0f;  // starts at origin end
    g.global_dest_node = global_dest_node;
    g.forced_retreat = false;
    g.retreating = false;
    g.time_elapsed = 0.0f;

    // Insert sorted by position (new group at 0.0 goes to front)
    auto& groups = el.lanes[lane_idx].groups;
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
    // A trailing group can only catch a leading group if it's smaller (faster).
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

        // Monotone optimization: if trailing is larger (slower), it can never
        // catch the leading group. Skip.
        if (trailing.count >= leading.count) {
            write = i;
            continue;
        }

        // Check distance (in position-space)
        float dist = leading.position - trailing.position;
        float threshold = (config.radius_factor * static_cast<float>(leading.count)
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
static void resolve_collisions(Lane& fwd, Lane& bwd, const GameConfig& config,
                               float edge_length) {
    if (fwd.groups.empty() || bwd.groups.empty()) return;

    // fwd: sorted by position ascending, frontmost is last (highest position)
    // bwd: sorted by position ascending in its own frame, frontmost is last
    // In edge frame: fwd front is at fwd.back().position
    //                bwd front is at 1.0 - bwd.back().position

    bool changed = true;
    while (changed) {
        changed = false;
        if (fwd.groups.empty() || bwd.groups.empty()) break;

        auto& fwd_front = fwd.groups.back();
        auto& bwd_front = bwd.groups.back();

        float fwd_pos = fwd_front.position;           // in edge frame
        float bwd_pos = 1.0f - bwd_front.position;    // in edge frame

        // They've met or crossed if fwd_pos >= bwd_pos
        if (fwd_pos >= bwd_pos) {
            // Skip if both already forced
            if (fwd_front.forced_retreat && bwd_front.forced_retreat) break;

            if (fwd_front.forced_retreat) {
                // fwd already forced, bwd wins by default
                break;
            }
            if (bwd_front.forced_retreat) {
                // bwd already forced, fwd wins by default
                break;
            }

            // Larger group wins; on tie, neither retreats (stalemate)
            if (fwd_front.count > bwd_front.count) {
                bwd_front.forced_retreat = true;
                bwd_front.retreating = true;
                changed = true;
            } else if (bwd_front.count > fwd_front.count) {
                fwd_front.forced_retreat = true;
                fwd_front.retreating = true;
                changed = true;
            }
            // Equal counts: stalemate, neither retreats
        }

        // After forcing a retreat, the retreating group will move backward.
        // The next frontmost group might now collide. But we only cascade
        // if the newly exposed leader hasn't been forced yet.
        // Since we only flip one group per iteration and check `changed`,
        // this naturally cascades.

        // However, the retreating group is still in the same lane at the same
        // position — it'll move away next tick. For this tick, no further
        // collision with the same opponent. Break to avoid infinite loop.
        break;
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
    resolve_collisions(el.lanes[0], el.lanes[1], config, el.edge_length);

    // 4. Extract arrivals
    // lane[0] = a->b: dest is node_b, origin is node_a
    // lane[1] = b->a: dest is node_a, origin is node_b
    extract_arrivals(el.lanes[0], el.node_b, el.node_a, arrivals_out);
    extract_arrivals(el.lanes[1], el.node_a, el.node_b, arrivals_out);
}
