#ifndef CRISKY_DISTRIBUTION_HPP
#define CRISKY_DISTRIBUTION_HPP

#include <vector>

struct TroopDistribution {
    std::vector<float> weights;  // sums to 1.0; weights[i] = desired troop fraction at node i
};

// Numerically stable softmax: d_i = exp(β*(A_i - max)) / Σ exp(β*(A_j - max))
// Nodes with zero scores contribute minimally at high β.
TroopDistribution softmax(const std::vector<float>& raw_scores, float beta);

// Weighted average of distributions: result[i] = Σ_k(w_k * dist_k[i]) / Σ_k(w_k)
TroopDistribution pool(const std::vector<const TroopDistribution*>& dists,
                        const std::vector<float>& weights);

// EMA smoothing: result[i] = α * current[i] + (1-α) * prev[i]
// Re-normalizes to sum to 1.0 after blending.
TroopDistribution ema(const TroopDistribution& current,
                       const TroopDistribution& prev, float alpha);

// Deprecated: use potential_deficit() from systems/transport/potential_solvers.hpp
// Kept for backward compatibility.
std::vector<float> distribution_to_gradient(const TroopDistribution& dist,
                                             const std::vector<int>& current_troops,
                                             int total_owned_troops);

#endif
