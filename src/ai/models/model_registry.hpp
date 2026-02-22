#ifndef CRISKY_MODEL_REGISTRY_HPP
#define CRISKY_MODEL_REGISTRY_HPP

#include "ai/models.hpp"

// Register a model factory by name. Called by per-model .cpp files.
void register_model(const char* name, ModelFactory factory);

// Register models that live in crisky_graph_algo (v5+, depend on Eigen/QUBO).
// Call this from apps that link crispy_graph_algo.
void register_graph_algo_models();

#endif
