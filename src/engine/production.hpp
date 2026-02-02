#ifndef CRISKY_PRODUCTION_HPP
#define CRISKY_PRODUCTION_HPP

#include <vector>
#include "game_config.hpp"
#include "game_state.hpp"
#include "graph.hpp"

// Produce troops at all nodes for one tick.
void produce_troops(std::vector<NodeData>& nodes, const Graph& graph,
                    const GameConfig& config);

#endif
