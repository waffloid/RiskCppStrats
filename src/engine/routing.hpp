#ifndef CRISKY_ROUTING_HPP
#define CRISKY_ROUTING_HPP

#include "graph.hpp"

// Given a current node and a global destination, return the best neighbor
// to route through (greedy dot-product heuristic).
// Returns -1 if current_node == global_dest or no neighbors exist.
int next_hop(const Graph& graph, int current_node, int global_dest);

#endif
