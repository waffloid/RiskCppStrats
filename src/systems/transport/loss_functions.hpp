#ifndef CRISKY_LOSS_FUNCTIONS_HPP
#define CRISKY_LOSS_FUNCTIONS_HPP

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// Loss functions measure mismatch between current and target troop distributions.
using LossFunction = std::function<float(
    const std::vector<int>& current,
    const std::vector<int>& target)>;

// L1: sum of absolute differences
inline float loss_l1(const std::vector<int>& current, const std::vector<int>& target) {
    float sum = 0.0f;
    for (size_t i = 0; i < current.size() && i < target.size(); i++) {
        sum += std::abs(static_cast<float>(current[i] - target[i]));
    }
    return sum;
}

// L2: root sum of squared differences
inline float loss_l2(const std::vector<int>& current, const std::vector<int>& target) {
    float sum_sq = 0.0f;
    for (size_t i = 0; i < current.size() && i < target.size(); i++) {
        float d = static_cast<float>(current[i] - target[i]);
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq);
}

// Max deficit: worst unmet demand (ignores surplus)
inline float loss_max_deficit(const std::vector<int>& current, const std::vector<int>& target) {
    float worst = 0.0f;
    for (size_t i = 0; i < current.size() && i < target.size(); i++) {
        float deficit = static_cast<float>(target[i] - current[i]);
        if (deficit > worst) worst = deficit;
    }
    return worst;
}

// Lookup by name
inline LossFunction get_loss_function(const std::string& name) {
    if (name == "l1") return loss_l1;
    if (name == "l2") return loss_l2;
    if (name == "max_deficit") return loss_max_deficit;
    return nullptr;
}

inline std::vector<std::string> list_loss_functions() {
    return {"l1", "l2", "max_deficit"};
}

#endif
