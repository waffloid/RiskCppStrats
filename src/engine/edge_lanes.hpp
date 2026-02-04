#ifndef CRISKY_EDGE_LANES_HPP
#define CRISKY_EDGE_LANES_HPP

#include <vector>
#include "game_config.hpp"

struct TroopGroup {
    int owner;
    int count;
    float position;          // 0.0 = origin node, 1.0 = destination node (normalized)
    int global_dest_node;    // for routing after arrival
    bool forced_retreat;     // prevents double-forcing in one traversal
    bool retreating;         // travelling back to origin
    float time_elapsed;      // ticks spent travelling (for retreat timing)
};

// One direction on an edge. Groups sorted by position ascending (toward dest).
struct Lane {
    std::vector<TroopGroup> groups;
};

enum class LaneDirection : int { A_TO_B = 0, B_TO_A = 1 };

// Arrival: a troop group that has reached its local destination
struct Arrival {
    int arrived_at_node;     // the node this group reached
    int owner;
    int count;
    int global_dest_node;
};

struct EdgeLanes {
    int edge_idx;
    int node_a, node_b;     // lane[0] = a->b, lane[1] = b->a
    float edge_length;
    Lane lanes[2];
};

// Compute per-tick displacement (in position-space, i.e., fraction of edge per tick)
float compute_displacement(int count, float dt, float edge_length, const GameConfig& config);

// Insert a new troop group into the appropriate lane of an edge.
// origin_node must be node_a or node_b of the EdgeLanes.
void insert_troop_group(EdgeLanes& el, int origin_node, int owner, int count,
                        int global_dest_node);

// Run one tick update on an edge's lanes. Returns arrivals.
void update_edge(EdgeLanes& el, float dt, const GameConfig& config,
                 std::vector<Arrival>& arrivals_out);

#endif
