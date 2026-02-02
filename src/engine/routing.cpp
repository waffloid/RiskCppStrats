#include "routing.hpp"
#include <cmath>
#include <limits>

int next_hop(const Graph& graph, int current_node, int global_dest) {
    if (current_node == global_dest) return -1;

    const auto& cur = graph.nodes[current_node];
    const auto& dest = graph.nodes[global_dest];

    if (cur.neighbor_indices.empty()) return -1;

    // Direction to global destination
    float dx = dest.x - cur.x;
    float dy = dest.y - cur.y;
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-9f) return -1;
    dx /= mag;
    dy /= mag;

    // Find neighbor with best dot-product similarity to destination direction
    int best_nbr = -1;
    float best_dot = -std::numeric_limits<float>::infinity();

    for (int nbr_idx : cur.neighbor_indices) {
        const auto& nbr = graph.nodes[nbr_idx];
        float ndx = nbr.x - cur.x;
        float ndy = nbr.y - cur.y;
        float nmag = std::sqrt(ndx * ndx + ndy * ndy);
        if (nmag < 1e-9f) continue;
        ndx /= nmag;
        ndy /= nmag;

        float dot = dx * ndx + dy * ndy;

        // If neighbor IS the destination, pick it immediately
        if (nbr_idx == global_dest) return nbr_idx;

        if (dot > best_dot) {
            best_dot = dot;
            best_nbr = nbr_idx;
        }
    }

    return best_nbr;
}
