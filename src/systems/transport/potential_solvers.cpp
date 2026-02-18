#include "systems/transport/potential_solvers.hpp"

#include <cmath>

void solve_graph_poisson(
    const Graph& graph,
    const std::vector<float>& deficit,
    std::vector<float>& phi,
    int max_iters,
    float omega) {

    int n = graph.num_nodes();
    // Cold start if phi is wrong size
    if (static_cast<int>(phi.size()) != n) {
        phi.assign(n, 0.0f);
    }

    for (int iter = 0; iter < max_iters; iter++) {
        for (int i = 0; i < n; i++) {
            int deg = graph.degree(i);
            if (deg == 0) continue;

            float nbr_sum = 0.0f;
            for (int j : graph.neighbors(i)) {
                nbr_sum += phi[j];
            }

            float gs = (deficit[i] + nbr_sum) / static_cast<float>(deg);
            phi[i] += omega * (gs - phi[i]);
        }

        // Check residual every 20 iterations
        if ((iter + 1) % 20 == 0) {
            float residual_sq = 0.0f;
            for (int i = 0; i < n; i++) {
                float Lphi_i = static_cast<float>(graph.degree(i)) * phi[i];
                for (int j : graph.neighbors(i)) {
                    Lphi_i -= phi[j];
                }
                float r = Lphi_i - deficit[i];
                residual_sq += r * r;
            }
            if (residual_sq < 1.0f) break;
        }
    }

}

std::vector<float> potential_deficit(
    const Graph& /*graph*/,
    const std::vector<int>& current_troops,
    const TroopDistribution& target,
    int total_troops) {

    int n = static_cast<int>(target.weights.size());
    std::vector<float> potential(n);
    float total = static_cast<float>(total_troops);

    for (int i = 0; i < n; i++) {
        float desired = target.weights[i] * total;
        potential[i] = desired - static_cast<float>(current_troops[i]);
    }

    return potential;
}
