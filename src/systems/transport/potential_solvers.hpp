#ifndef CRISKY_POTENTIAL_SOLVERS_HPP
#define CRISKY_POTENTIAL_SOLVERS_HPP

#include <functional>
#include <vector>
#include "ai/distribution.hpp"
#include "engine/graph.hpp"

// A PotentialSolver computes per-node potential from current state and target
// distribution. The transport solver then moves troops along the potential
// gradient: flow on edge (i,j) ∝ potential[j] - potential[i].
//
// potential[i] > 0 means "need more troops here" (deficit)
// potential[i] < 0 means "excess troops here" (surplus)
using PotentialSolver = std::function<std::vector<float>(
    const Graph& graph,
    const std::vector<int>& current_troops,
    const TroopDistribution& target,
    int total_troops)>;

// Default: simple deficit potential.
// potential[i] = target_fraction[i] * total - current[i]
// Curl-free by construction (induced by a scalar field).
std::vector<float> potential_deficit(
    const Graph& graph,
    const std::vector<int>& current_troops,
    const TroopDistribution& target,
    int total_troops);

#endif
