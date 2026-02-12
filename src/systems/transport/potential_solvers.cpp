#include "systems/transport/potential_solvers.hpp"

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
