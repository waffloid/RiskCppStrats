#include "ai/models/model_registry.hpp"

// Forward declarations of models in crisky_graph_algo (depend on Eigen/QUBO).
extern void register_v5_qubo();
extern void register_v7();
extern void register_v8();
extern void register_v9();
extern void register_v10();
extern void register_v11();
extern void register_v12();

void register_graph_algo_models() {
    register_v5_qubo();
    register_v7();
    register_v8();
    register_v9();
    register_v10();
    register_v11();
    register_v12();
}
