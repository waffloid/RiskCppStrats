#include "graph.hpp"

#include <algorithm>
#include <random>

Graph Graph::generate_poisson(const GameConfig& config, uint64_t seed) {
    Graph g;
    std::mt19937_64 rng(seed);

    // Poisson process: expected count = intensity * area
    float area = config.region_width * config.region_height;
    float expected_count = config.poisson_intensity * area;
    std::poisson_distribution<int> count_dist(expected_count);
    int n = count_dist(rng);

    // Sample points uniformly in the region
    std::uniform_real_distribution<float> x_dist(0.0f, config.region_width);
    std::uniform_real_distribution<float> y_dist(0.0f, config.region_height);

    g.nodes.reserve(n);
    for (int i = 0; i < n; i++) {
        Node node;
        node.x = x_dist(rng);
        node.y = y_dist(rng);
        node.idx = i;
        g.nodes.push_back(node);
    }

    // Build edges between nodes within distance threshold
    g.build_edges(config.edge_distance_threshold);

    // Cull nodes with too many neighbors
    g.cull_high_degree(config.max_neighbors);

    // Rebuild edge index after culling
    g.build_edge_index();

    return g;
}

void Graph::build_edges(float distance_threshold) {
    float thresh_sq = distance_threshold * distance_threshold;
    int n = num_nodes();

    edges.clear();
    for (auto& node : nodes) {
        node.neighbor_indices.clear();
    }

    // Brute force O(n^2) — fine for expected graph sizes.
    // Could use spatial hashing if this becomes a bottleneck.
    int edge_idx = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            float dx = nodes[i].x - nodes[j].x;
            float dy = nodes[i].y - nodes[j].y;
            float dist_sq = dx * dx + dy * dy;

            if (dist_sq <= thresh_sq) {
                Edge e;
                e.a_idx = i;
                e.b_idx = j;
                e.idx = edge_idx++;
                e.length = std::sqrt(dist_sq);

                edges.push_back(e);
                nodes[i].neighbor_indices.push_back(j);
                nodes[j].neighbor_indices.push_back(i);
            }
        }
    }
}

void Graph::cull_high_degree(int max_neighbors) {
    // Iteratively remove the highest-degree node until all are within limit.
    // This preserves more graph structure than removing all violators at once.
    // Precompute edge lengths for each node's neighbors
    // node_neighbor_dist[i] maps neighbor_idx -> distance
    std::vector<std::unordered_map<int, float>> node_neighbor_dist(num_nodes());
    for (const auto& e : edges) {
        node_neighbor_dist[e.a_idx][e.b_idx] = e.length;
        node_neighbor_dist[e.b_idx][e.a_idx] = e.length;
    }

    std::vector<bool> removed(num_nodes(), false);
    for (;;) {
        int worst = -1;
        float worst_score = -1.0f;
        for (const auto& node : nodes) {
            if (removed[node.idx]) continue;
            // Count live neighbors and sum distances to them
            int deg = 0;
            float dist_sum = 0.0f;
            for (int nb : node.neighbor_indices) {
                if (removed[nb]) continue;
                deg++;
                auto it = node_neighbor_dist[node.idx].find(nb);
                if (it != node_neighbor_dist[node.idx].end())
                    dist_sum += it->second;
            }
            if (deg <= max_neighbors) continue;
            // Among violators, prefer to remove the most clustered:
            // lowest sum of neighbor distances (tightly packed)
            // Invert so that smallest dist_sum = highest score
            float score = static_cast<float>(deg) / (dist_sum + 0.001f);
            if (score > worst_score) {
                worst_score = score;
                worst = node.idx;
            }
        }
        if (worst < 0) break;
        removed[worst] = true;
    }

    // Build new node list, mapping old indices to new
    std::vector<int> old_to_new(num_nodes(), -1);
    std::vector<Node> new_nodes;
    int new_idx = 0;
    for (const auto& node : nodes) {
        if (!removed[node.idx]) {
            old_to_new[node.idx] = new_idx;
            Node n = node;
            n.idx = new_idx++;
            n.neighbor_indices.clear(); // rebuilt below
            new_nodes.push_back(n);
        }
    }

    // Rebuild edges, skipping any that reference removed nodes
    std::vector<Edge> new_edges;
    int eidx = 0;
    for (const auto& e : edges) {
        int na = old_to_new[e.a_idx];
        int nb = old_to_new[e.b_idx];
        if (na >= 0 && nb >= 0) {
            Edge ne;
            ne.a_idx = (na < nb) ? na : nb;
            ne.b_idx = (na < nb) ? nb : na;
            ne.idx = eidx++;
            ne.length = e.length;
            new_edges.push_back(ne);
            new_nodes[na].neighbor_indices.push_back(nb);
            new_nodes[nb].neighbor_indices.push_back(na);
        }
    }

    nodes = std::move(new_nodes);
    edges = std::move(new_edges);
}

void Graph::build_edge_index() {
    node_pair_to_edge_idx.clear();
    node_pair_to_edge_idx.reserve(edges.size());
    for (const auto& e : edges) {
        node_pair_to_edge_idx[pack_pair(e.a_idx, e.b_idx)] = e.idx;
    }
}

int Graph::edge_between(int node_a, int node_b) const {
    auto it = node_pair_to_edge_idx.find(pack_pair(node_a, node_b));
    if (it != node_pair_to_edge_idx.end()) {
        return it->second;
    }
    return -1;
}

Graph Graph::subgraph(const std::vector<int>& node_indices) const {
    Graph g;

    std::vector<int> old_to_new(num_nodes(), -1);
    for (int i = 0; i < static_cast<int>(node_indices.size()); i++) {
        old_to_new[node_indices[i]] = i;
    }

    g.nodes.reserve(node_indices.size());
    for (int i = 0; i < static_cast<int>(node_indices.size()); i++) {
        const Node& old = nodes[node_indices[i]];
        Node n;
        n.x = old.x;
        n.y = old.y;
        n.idx = i;
        // neighbors filled below
        g.nodes.push_back(n);
    }

    int eidx = 0;
    for (const auto& e : edges) {
        int na = old_to_new[e.a_idx];
        int nb = old_to_new[e.b_idx];
        if (na >= 0 && nb >= 0) {
            Edge ne;
            ne.a_idx = (na < nb) ? na : nb;
            ne.b_idx = (na < nb) ? nb : na;
            ne.idx = eidx++;
            ne.length = e.length;
            g.edges.push_back(ne);
            g.nodes[na].neighbor_indices.push_back(nb);
            g.nodes[nb].neighbor_indices.push_back(na);
        }
    }

    g.build_edge_index();
    return g;
}
