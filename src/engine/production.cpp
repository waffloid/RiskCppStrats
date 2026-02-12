#include "production.hpp"

void produce_troops(std::vector<NodeData>& nodes, const Graph& graph,
                    const GameConfig& config) {
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        NodeData& nd = nodes[i];
        if (nd.owner < 0) continue;

        int base_production = 0;
        switch (nd.state) {
            case NodeState::CAPITAL:
                base_production = config.capital_troops_per_tick;
                break;
            case NodeState::FACTORY:
                base_production = config.factory_troops_per_tick;
                break;
            default:
                break;
        }

        // Powerplant bonus: check if any neighbor is a powerplant owned by same player
        if (nd.state == NodeState::CAPITAL || nd.state == NodeState::FACTORY) {
            const Node& node = graph.nodes[i];
            for (int nbr_idx : graph.neighbors(i)) {
                const NodeData& nbr = nodes[nbr_idx];
                if (nbr.state == NodeState::POWERPLANT && nbr.owner == nd.owner) {
                    base_production += config.powerplant_bonus;
                }
            }
        }

        if (base_production > 0) {
            nd.troops[nd.owner] += base_production;
        }
    }
}
