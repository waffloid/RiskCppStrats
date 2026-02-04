#ifndef CRISKY_GRAPH_BUILDER_HPP
#define CRISKY_GRAPH_BUILDER_HPP

#include "graph.hpp"
#include <utility>
#include <vector>

// Build a graph from explicit node positions and edge list.
// Edges are specified as (node_a, node_b) pairs; a < b convention is enforced internally.
Graph build_graph(const std::vector<std::pair<float, float>>& positions,
                  const std::vector<std::pair<int, int>>& edge_list);

// Build a complete bipartite graph K_{n,m}.
// Left partition: nodes 0..n-1 at x=0, spaced vertically by `spacing`.
// Right partition: nodes n..n+m-1 at x=separation, spaced vertically by `spacing`.
// Every left node is connected to every right node.
Graph build_bipartite(int n, int m, float spacing = 10.0f, float separation = 40.0f);

// Build a path graph: 0 -- 1 -- 2 -- ... -- (count-1).
// Nodes placed along x-axis, spaced by `spacing`.
Graph build_path(int count, float spacing = 15.0f);

// Build a star graph: center node 0 connected to nodes 1..n.
// Leaves placed in a circle of given radius around the center.
Graph build_star(int n_leaves, float radius = 15.0f);

#endif
