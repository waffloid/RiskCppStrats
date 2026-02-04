#include "engine/graph_builder.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

Graph build_graph(const std::vector<std::pair<float, float>>& positions,
                  const std::vector<std::pair<int, int>>& edge_list) {
    Graph g;

    // Create nodes
    int n = static_cast<int>(positions.size());
    g.nodes.resize(n);
    for (int i = 0; i < n; i++) {
        g.nodes[i].x = positions[i].first;
        g.nodes[i].y = positions[i].second;
        g.nodes[i].idx = i;
    }

    // Create edges
    int edge_idx = 0;
    for (auto [a, b] : edge_list) {
        int lo = std::min(a, b);
        int hi = std::max(a, b);

        float dx = g.nodes[hi].x - g.nodes[lo].x;
        float dy = g.nodes[hi].y - g.nodes[lo].y;
        float length = std::sqrt(dx * dx + dy * dy);

        Edge e;
        e.a_idx = lo;
        e.b_idx = hi;
        e.idx = edge_idx;
        e.length = length;
        g.edges.push_back(e);

        // Populate neighbor lists (bidirectional)
        g.nodes[lo].neighbor_indices.push_back(hi);
        g.nodes[hi].neighbor_indices.push_back(lo);

        // Edge index
        g.node_pair_to_edge_idx[Graph::pack_pair(lo, hi)] = edge_idx;

        edge_idx++;
    }

    return g;
}

Graph build_bipartite(int n, int m, float spacing, float separation) {
    std::vector<std::pair<float, float>> positions;
    positions.reserve(n + m);

    // Left partition: nodes 0..n-1
    float left_offset = -(static_cast<float>(n - 1) * spacing) / 2.0f;
    for (int i = 0; i < n; i++) {
        positions.push_back({0.0f, left_offset + static_cast<float>(i) * spacing});
    }

    // Right partition: nodes n..n+m-1
    float right_offset = -(static_cast<float>(m - 1) * spacing) / 2.0f;
    for (int j = 0; j < m; j++) {
        positions.push_back({separation, right_offset + static_cast<float>(j) * spacing});
    }

    // Complete bipartite: every left connected to every right
    std::vector<std::pair<int, int>> edges;
    edges.reserve(n * m);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            edges.push_back({i, n + j});
        }
    }

    return build_graph(positions, edges);
}

Graph build_path(int count, float spacing) {
    std::vector<std::pair<float, float>> positions;
    positions.reserve(count);
    for (int i = 0; i < count; i++) {
        positions.push_back({static_cast<float>(i) * spacing, 0.0f});
    }

    std::vector<std::pair<int, int>> edges;
    edges.reserve(count - 1);
    for (int i = 0; i < count - 1; i++) {
        edges.push_back({i, i + 1});
    }

    return build_graph(positions, edges);
}

Graph build_star(int n_leaves, float radius) {
    std::vector<std::pair<float, float>> positions;
    positions.reserve(1 + n_leaves);

    // Center node at origin
    positions.push_back({0.0f, 0.0f});

    // Leaves evenly spaced around circle
    for (int i = 0; i < n_leaves; i++) {
        float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i)
                      / static_cast<float>(n_leaves);
        positions.push_back({radius * std::cos(angle), radius * std::sin(angle)});
    }

    std::vector<std::pair<int, int>> edges;
    edges.reserve(n_leaves);
    for (int i = 0; i < n_leaves; i++) {
        edges.push_back({0, i + 1});
    }

    return build_graph(positions, edges);
}
