#include "ai/distribution.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

TroopDistribution softmax(const std::vector<float>& raw_scores, float beta) {
    TroopDistribution dist;
    int n = static_cast<int>(raw_scores.size());
    dist.weights.resize(n);

    if (n == 0) return dist;

    // Numerically stable: subtract max before exponentiating
    float max_score = *std::max_element(raw_scores.begin(), raw_scores.end());
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        dist.weights[i] = std::exp(beta * (raw_scores[i] - max_score));
        sum += dist.weights[i];
    }

    if (sum > 0.0f) {
        float inv_sum = 1.0f / sum;
        for (int i = 0; i < n; i++) dist.weights[i] *= inv_sum;
    } else {
        // Degenerate: uniform
        float uniform = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; i++) dist.weights[i] = uniform;
    }

    return dist;
}

TroopDistribution pool(const std::vector<const TroopDistribution*>& dists,
                        const std::vector<float>& weights) {
    TroopDistribution result;
    if (dists.empty()) return result;

    int n = static_cast<int>(dists[0]->weights.size());
    result.weights.assign(n, 0.0f);
    float total_weight = 0.0f;

    for (int k = 0; k < static_cast<int>(dists.size()); k++) {
        float w = weights[k];
        total_weight += w;
        for (int i = 0; i < n; i++) {
            result.weights[i] += w * dists[k]->weights[i];
        }
    }

    if (total_weight > 0.0f) {
        float inv = 1.0f / total_weight;
        for (int i = 0; i < n; i++) result.weights[i] *= inv;
    }

    return result;
}

TroopDistribution ema(const TroopDistribution& current,
                       const TroopDistribution& prev, float alpha) {
    TroopDistribution result;
    int n = static_cast<int>(current.weights.size());
    result.weights.resize(n);

    // If prev is empty (first tick), just return current
    if (prev.weights.empty()) return current;

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        result.weights[i] = alpha * current.weights[i] + (1.0f - alpha) * prev.weights[i];
        sum += result.weights[i];
    }

    // Re-normalize
    if (sum > 0.0f) {
        float inv = 1.0f / sum;
        for (int i = 0; i < n; i++) result.weights[i] *= inv;
    }

    return result;
}

std::vector<float> distribution_to_gradient(const TroopDistribution& dist,
                                             const std::vector<int>& current_troops,
                                             int total_owned_troops) {
    int n = static_cast<int>(dist.weights.size());
    std::vector<float> gradient(n);
    float total = static_cast<float>(total_owned_troops);

    for (int i = 0; i < n; i++) {
        float desired = dist.weights[i] * total;
        gradient[i] = desired - static_cast<float>(current_troops[i]);
    }

    return gradient;
}
