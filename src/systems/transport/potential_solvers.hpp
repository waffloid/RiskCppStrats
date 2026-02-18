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

// Solve the graph Poisson equation L*phi = deficit using Gauss-Seidel
// with successive over-relaxation. Solves in-place: phi is both the initial
// guess and the output. Pass a warm phi from the previous tick for inertia,
// or resize to num_nodes and zero-fill for a cold start.
void solve_graph_poisson(
    const Graph& graph,
    const std::vector<float>& deficit,
    std::vector<float>& phi,
    int max_iters = 1000,
    float omega = 1.5f);

#endif
