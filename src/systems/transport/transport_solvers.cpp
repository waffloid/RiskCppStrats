#include "systems/transport/transport_solvers.hpp"

#include <algorithm>
#include <cmath>

std::vector<TroopCommand> transport_solver_greedy(
    const Graph& graph,
    const std::vector<NodeData>& nodes,
    const std::vector<float>& potential,
    int player_id,
    const std::vector<bool>& masked,
    float outflow_rate,
    int min_troops) {

    std::vector<TroopCommand> commands;
    std::vector<float> pos_diff;

    for (int node = 0; node < graph.num_nodes(); node++) {
        const NodeData& nd = nodes[node];
        if (nd.owner != player_id) continue;
        if (masked[node]) continue;

        int troops_here = nd.troops[player_id];
        if (troops_here < min_troops * 2) continue;

        const std::vector<int>& nbrs = graph.neighbors(node);
        if (nbrs.empty()) continue;

        // Positive potential differences to neighbors
        float self_pot = potential[node];
        pos_diff.resize(nbrs.size());
        float norm_sq = 0.0f;
        for (size_t i = 0; i < nbrs.size(); i++) {
            float diff = potential[nbrs[i]] - self_pot;
            pos_diff[i] = (diff > 0.0f) ? diff : 0.0f;
            norm_sq += pos_diff[i] * pos_diff[i];
        }

        float norm_pos = std::sqrt(norm_sq);
        if (norm_pos < 1e-6f) continue;

        // Sigmoid-weighted outflow
        float clamped = std::min(norm_pos, 20.0f);
        float exp_val = std::exp(clamped);
        float outflow_fraction = (exp_val / (1.0f + exp_val)) * outflow_rate;

        float total_to_send = static_cast<float>(troops_here) * outflow_fraction;
        if (total_to_send < static_cast<float>(min_troops)) continue;

        // Send proportionally to each neighbor with positive potential diff
        for (size_t i = 0; i < nbrs.size(); i++) {
            float allocation = pos_diff[i] / norm_pos;
            int amount = static_cast<int>(total_to_send * allocation);
            if (amount >= min_troops) {
                commands.push_back({node, nbrs[i], amount});
            }
        }
    }

    return commands;
}

TransportSolver get_transport_solver(const std::string& name) {
    if (name == "greedy") {
        return [](const Graph& g, const std::vector<NodeData>& n,
                  const std::vector<float>& grad, int pid,
                  const std::vector<bool>& m) {
            return transport_solver_greedy(g, n, grad, pid, m, 0.1f, 1);
        };
    }
    return nullptr;
}

std::vector<std::string> list_transport_solvers() {
    return {"greedy", "ot"};
}
