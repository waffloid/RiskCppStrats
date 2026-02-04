#ifndef CRISKY_GRAPH_HPP
#define CRISKY_GRAPH_HPP

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game_config.hpp"

struct Node {
    float x, y;
    int idx;
    std::vector<int> neighbor_indices;
};

struct Edge {
    int a_idx, b_idx;   // a_idx < b_idx by convention
    int idx;
    float length;       // precomputed Euclidean distance
};

class Graph {
public:
    std::vector<Node> nodes;
    std::vector<Edge> edges;

    // O(1) edge lookup: pack (min_idx, max_idx) into uint64_t key
    std::unordered_map<uint64_t, int> node_pair_to_edge_idx;

    static Graph generate_poisson(const GameConfig& config, uint64_t seed);

    // Subgraph containing only the specified node indices.
    // Returned graph has re-indexed nodes (0..n-1) but preserves positions.
    Graph subgraph(const std::vector<int>& node_indices) const;

    int edge_between(int node_a, int node_b) const;

    int num_nodes() const { return static_cast<int>(nodes.size()); }
    int num_edges() const { return static_cast<int>(edges.size()); }

    static uint64_t pack_pair(int a, int b) {
        int lo = (a < b) ? a : b;
        int hi = (a < b) ? b : a;
        return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
    }

private:
    void cull_high_degree(int max_neighbors);
    void build_edges(float distance_threshold);
    void build_edge_index();
};

#endif
