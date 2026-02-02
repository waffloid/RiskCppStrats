#ifndef CRISKY_COMBAT_HPP
#define CRISKY_COMBAT_HPP

#include <vector>
#include "game_config.hpp"
#include "game_state.hpp"
#include "graph.hpp"

// Resolve combat at a single node. Modifies node_data.troops in place.
// all_nodes is needed for artillery (boosts attack at neighboring nodes).
void resolve_combat(NodeData& node_data, int node_idx,
                    const Graph& graph, const std::vector<NodeData>& all_nodes,
                    int n_players, const GameConfig& config);

#endif
