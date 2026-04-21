#include "systems/graph_algo/maxcut_solvers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>

#include <Eigen/Dense>

// ── QUBOInstance ──────────────────────────────────────────────

QUBOInstance QUBOInstance::from_graph(const Graph& graph,
                                      const std::vector<float>& edge_weights) {
    QUBOInstance inst;
    inst.n = graph.num_nodes();
    inst.graph = &graph;
    inst.Q.assign(inst.n, std::vector<float>(inst.n, 0.0f));
    for (const auto& e : graph.edges) {
        float w = edge_weights.empty() ? 1.0f : edge_weights[e.idx];
        inst.Q[e.a_idx][e.b_idx] = w;
        inst.Q[e.b_idx][e.a_idx] = w;
    }
    return inst;
}

// ── Utilities ────────────────────────────────────────────────

float compute_qubo_objective(const QUBOInstance& instance,
                              const std::vector<int>& partition) {
    // Ising-style objective:
    //   H(x) = sum_i Q[i][i]*x_i  +  sum_{i<j} 2*Q[i][j]*x_i*x_j
    // Q[i][i] = linear field (bias toward +1 or -1)
    // Q[i][j] = quadratic coupling
    float val = 0.0f;
    int n = instance.n;
    for (int i = 0; i < n; i++) {
        // Linear term
        val += instance.Q[i][i] * static_cast<float>(partition[i]);
        // Quadratic terms — upper triangle, symmetric so count once × 2
        for (int j = i + 1; j < n; j++) {
            if (instance.Q[i][j] != 0.0f) {
                val += 2.0f * instance.Q[i][j] *
                       static_cast<float>(partition[i]) *
                       static_cast<float>(partition[j]);
            }
        }
    }
    return val;
}

std::vector<float> compute_qubo_flip_gains(const QUBOInstance& instance,
                                            const std::vector<int>& partition) {
    int n = instance.n;
    std::vector<float> gains(n, 0.0f);
    // Ising objective: H = sum_i h_i*x_i + sum_{i<j} 2*J_ij*x_i*x_j
    // where h_i = Q[i][i], J_ij = Q[i][j].
    //
    // Flipping x_i → -x_i:
    //   Linear change: h_i*(-x_i) - h_i*x_i = -2*h_i*x_i
    //   Quadratic change: for each j≠i, 2*J_ij*(-x_i)*x_j - 2*J_ij*x_i*x_j
    //                   = -4*J_ij*x_i*x_j
    //   Total delta = -2*x_i*(h_i + 2*sum_{j≠i} J_ij*x_j)
    for (int i = 0; i < n; i++) {
        float field = instance.Q[i][i];  // linear bias
        for (int j = 0; j < n; j++) {
            if (j != i && instance.Q[i][j] != 0.0f) {
                field += 2.0f * instance.Q[i][j] * static_cast<float>(partition[j]);
            }
        }
        gains[i] = -2.0f * static_cast<float>(partition[i]) * field;
    }
    return gains;
}

// Sparse flip-gain computation using graph topology (O(degree) per node).
static std::vector<float> compute_flip_gains_sparse(
    const QUBOInstance& instance,
    const std::vector<int>& partition) {
    if (!instance.graph) return compute_qubo_flip_gains(instance, partition);
    int n = instance.n;
    std::vector<float> gains(n, 0.0f);
    for (int i = 0; i < n; i++) {
        float field = instance.Q[i][i];  // linear bias
        for (int nbr : instance.graph->neighbors(i)) {
            field += 2.0f * instance.Q[i][nbr] * static_cast<float>(partition[nbr]);
        }
        gains[i] = -2.0f * static_cast<float>(partition[i]) * field;
    }
    return gains;
}

// ── Greedy local search ──────────────────────────────────────

QUBOSolution qubo_solve_greedy(const QUBOInstance& instance, uint64_t seed) {
    auto t0 = std::chrono::high_resolution_clock::now();
    int n = instance.n;

    // Random initial partition
    std::mt19937 rng(seed);
    std::vector<int> partition(n);
    for (int i = 0; i < n; i++) {
        partition[i] = (rng() % 2 == 0) ? 1 : -1;
    }

    auto flip_gain = compute_flip_gains_sparse(instance, partition);
    float obj = compute_qubo_objective(instance, partition);
    int total_iters = 0;

    bool improved = true;
    while (improved) {
        improved = false;
        for (int i = 0; i < n; i++) {
            if (flip_gain[i] > 1e-8f) {
                obj += flip_gain[i];
                partition[i] = -partition[i];

                // Update flip gains for neighbors.
                // When x_i flips, each neighbor j's field changes by
                // 2*Q[i][j]*(x_i_new - x_i_old) = 2*Q[i][j]*2*x_i_new = 4*Q[i][j]*x_i_new
                // And j's gain = -2*x_j*field_j, so delta_gain_j = -2*x_j*4*Q[i][j]*x_i_new
                //              = -8*Q[i][j]*x_i_new*x_j
                if (instance.graph) {
                    for (int nbr : instance.graph->neighbors(i)) {
                        flip_gain[nbr] += -8.0f * instance.Q[i][nbr] *
                                          static_cast<float>(partition[i]) *
                                          static_cast<float>(partition[nbr]);
                    }
                } else {
                    for (int j = 0; j < n; j++) {
                        if (j != i && instance.Q[i][j] != 0.0f) {
                            flip_gain[j] += -8.0f * instance.Q[i][j] *
                                            static_cast<float>(partition[i]) *
                                            static_cast<float>(partition[j]);
                        }
                    }
                }
                // Node i's own flip gain: after flipping, exact negation
                flip_gain[i] = -flip_gain[i];

                improved = true;
                total_iters++;
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

    return {partition, obj, flip_gain, total_iters, ms, "greedy"};
}

// ── Simulated annealing ──────────────────────────────────────

QUBOSolution qubo_solve_sa(const QUBOInstance& instance, uint64_t seed,
                            int iterations, float initial_temp) {
    return qubo_solve_sa_traced(instance, seed, iterations, initial_temp, nullptr);
}

QUBOSolution qubo_solve_sa_traced(const QUBOInstance& instance, uint64_t seed,
                                   int iterations, float initial_temp,
                                   QUBOTrace* trace) {
    auto t0 = std::chrono::high_resolution_clock::now();
    int n = instance.n;
    if (n == 0) return {};

    // Warm start from greedy
    auto greedy = qubo_solve_greedy(instance, seed);
    auto partition = greedy.partition;
    auto flip_gain = compute_flip_gains_sparse(instance, partition);
    float obj = greedy.objective;
    float best_obj = obj;
    auto best_partition = partition;

    std::mt19937 rng(seed + 1);
    std::uniform_int_distribution<int> node_dist(0, n - 1);
    std::uniform_real_distribution<float> accept_dist(0.0f, 1.0f);

    if (trace) {
        trace->initial_objective = obj;
        trace->iterations = iterations;
        trace->accepted = 0;
        trace->improvements = 0;
    }

    for (int iter = 0; iter < iterations; iter++) {
        float temp = initial_temp * (1.0f - static_cast<float>(iter) / static_cast<float>(iterations));
        temp = std::max(temp, 0.01f);

        int i = node_dist(rng);
        float delta = flip_gain[i];

        bool accept = false;
        if (delta > 0.0f) {
            accept = true;
            if (trace) trace->improvements++;
        } else if (temp > 0.01f) {
            float prob = std::exp(delta / temp);
            accept = accept_dist(rng) < prob;
        }

        if (accept) {
            obj += delta;
            partition[i] = -partition[i];

            // Incremental flip-gain update
            if (instance.graph) {
                for (int nbr : instance.graph->neighbors(i)) {
                    flip_gain[nbr] += -8.0f * instance.Q[i][nbr] *
                                      static_cast<float>(partition[i]) *
                                      static_cast<float>(partition[nbr]);
                }
            } else {
                for (int j = 0; j < n; j++) {
                    if (j != i && instance.Q[i][j] != 0.0f) {
                        flip_gain[j] += -8.0f * instance.Q[i][j] *
                                        static_cast<float>(partition[i]) *
                                        static_cast<float>(partition[j]);
                    }
                }
            }
            flip_gain[i] = -flip_gain[i];

            if (trace) trace->accepted++;
            if (obj > best_obj) {
                best_obj = obj;
                best_partition = partition;
            }
        }

        if (trace) {
            trace->objective_per_iter.push_back(obj);
            trace->best_objective_per_iter.push_back(best_obj);
        }
    }

    if (trace) trace->final_objective = best_obj;

    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

    auto best_gains = compute_flip_gains_sparse(instance, best_partition);
    return {best_partition, best_obj, best_gains, iterations, ms, "sa"};
}

// ── QUBOStepper ──────────────────────────────────────────────

QUBOStepper::QUBOStepper(const QUBOInstance& instance, uint64_t seed,
                          int total_iterations, float initial_temp)
    : instance_(&instance), total_iters_(total_iterations),
      initial_temp_(initial_temp), rng_(seed) {
    init_greedy_warmstart();
}

void QUBOStepper::init_greedy_warmstart() {
    auto greedy = qubo_solve_greedy(*instance_, rng_());
    current_ = greedy.partition;
    best_ = current_;
    flip_gain_ = compute_flip_gains_sparse(*instance_, current_);
    current_obj_ = greedy.objective;
    best_obj_ = current_obj_;
    iter_ = 0;
    trace_ = QUBOTrace{};
    trace_.initial_objective = current_obj_;
    trace_.iterations = total_iters_;
}

void QUBOStepper::reset(int total_iterations, float initial_temp) {
    total_iters_ = total_iterations;
    initial_temp_ = initial_temp;
    init_greedy_warmstart();
}

void QUBOStepper::reset_seed(uint64_t seed) {
    rng_.seed(seed);
    init_greedy_warmstart();
}

void QUBOStepper::update_flip_gains_after_flip(int node) {
    if (instance_->graph) {
        for (int nbr : instance_->graph->neighbors(node)) {
            flip_gain_[nbr] += -8.0f * instance_->Q[node][nbr] *
                               static_cast<float>(current_[node]) *
                               static_cast<float>(current_[nbr]);
        }
    } else {
        int n = instance_->n;
        for (int j = 0; j < n; j++) {
            if (j != node && instance_->Q[node][j] != 0.0f) {
                flip_gain_[j] += -8.0f * instance_->Q[node][j] *
                                 static_cast<float>(current_[node]) *
                                 static_cast<float>(current_[j]);
            }
        }
    }
    flip_gain_[node] = -flip_gain_[node];
}

bool QUBOStepper::step(int n_iters) {
    int n = instance_->n;
    if (n == 0) return false;

    std::uniform_int_distribution<int> node_dist(0, n - 1);
    std::uniform_real_distribution<float> accept_dist(0.0f, 1.0f);

    int end = iter_ + n_iters;
    for (; iter_ < end; iter_++) {
        float temp = initial_temp_ * (1.0f - static_cast<float>(iter_) /
                     static_cast<float>(std::max(1, total_iters_)));
        temp = std::max(temp, 0.01f);

        int i = node_dist(rng_);
        float delta = flip_gain_[i];

        bool accept = false;
        if (delta > 0.0f) {
            accept = true;
            trace_.improvements++;
        } else if (temp > 0.01f) {
            float prob = std::exp(delta / temp);
            accept = accept_dist(rng_) < prob;
        }

        if (accept) {
            current_obj_ += delta;
            current_[i] = -current_[i];
            update_flip_gains_after_flip(i);
            trace_.accepted++;
            if (current_obj_ > best_obj_) {
                best_obj_ = current_obj_;
                best_ = current_;
            }
        }

        trace_.objective_per_iter.push_back(current_obj_);
        trace_.best_objective_per_iter.push_back(best_obj_);
    }

    trace_.final_objective = best_obj_;
    return true;
}

// ── Goemans-Williamson SDP ───────────────────────────────────

QUBOSolution qubo_solve_gw(const QUBOInstance& instance, uint64_t seed,
                            int sdp_iterations, int rounding_trials) {
    auto t0 = std::chrono::high_resolution_clock::now();
    int n = instance.n;
    if (n == 0) return {};

    // Build weight matrix W as Eigen matrix (use off-diagonal of Q only
    // for the SDP relaxation; linear terms handled via rounding bias).
    Eigen::MatrixXf W = Eigen::MatrixXf::Zero(n, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i != j) W(i, j) = instance.Q[i][j];
        }
    }

    // SDP relaxation: max (1/2) tr(W * (I - V^T V))
    // Variables: V ∈ R^{k×n} where each column v_i is a unit vector.
    // Gradient w.r.t. V: dL/dV = -W * V^T  (simplified)
    // We work with V as n×k (rows are unit vectors).
    int k = std::min(n, 20);
    std::mt19937 rng(seed);
    std::normal_distribution<float> normal(0.0f, 1.0f);

    Eigen::MatrixXf V(n, k);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < k; j++) {
            V(i, j) = normal(rng);
        }
        V.row(i).normalize();
    }

    // Projected gradient ascent
    float lr = 0.01f;
    for (int iter = 0; iter < sdp_iterations; iter++) {
        // Gradient: for max sum_{i<j} w_ij * (1 - v_i . v_j) / 2
        // dL/dv_i = -sum_j w_ij * v_j / 2
        Eigen::MatrixXf grad = -W * V * 0.5f;

        V -= lr * grad;  // gradient ascent (negative of negative gradient)

        // Project rows back to unit sphere
        for (int i = 0; i < n; i++) {
            float norm = V.row(i).norm();
            if (norm > 1e-8f) V.row(i) /= norm;
        }
    }

    // Random hyperplane rounding
    float best_obj = -1e30f;
    std::vector<int> best_partition(n, 1);

    for (int trial = 0; trial < rounding_trials; trial++) {
        Eigen::VectorXf r(k);
        for (int j = 0; j < k; j++) r(j) = normal(rng);
        r.normalize();

        std::vector<int> partition(n);
        for (int i = 0; i < n; i++) {
            partition[i] = (V.row(i).dot(r) >= 0.0f) ? 1 : -1;
        }

        float obj = compute_qubo_objective(instance, partition);
        if (obj > best_obj) {
            best_obj = obj;
            best_partition = partition;
        }
    }

    // Polish with greedy
    auto flip_gain = compute_flip_gains_sparse(instance, best_partition);
    bool improved = true;
    while (improved) {
        improved = false;
        for (int i = 0; i < n; i++) {
            if (flip_gain[i] > 1e-8f) {
                best_obj += flip_gain[i];
                best_partition[i] = -best_partition[i];
                if (instance.graph) {
                    for (int nbr : instance.graph->neighbors(i)) {
                        flip_gain[nbr] += -8.0f * instance.Q[i][nbr] *
                                          static_cast<float>(best_partition[i]) *
                                          static_cast<float>(best_partition[nbr]);
                    }
                } else {
                    for (int j = 0; j < n; j++) {
                        if (j != i && instance.Q[i][j] != 0.0f) {
                            flip_gain[j] += -8.0f * instance.Q[i][j] *
                                            static_cast<float>(best_partition[i]) *
                                            static_cast<float>(best_partition[j]);
                        }
                    }
                }
                flip_gain[i] = -flip_gain[i];
                improved = true;
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

    auto gains = compute_flip_gains_sparse(instance, best_partition);
    return {best_partition, best_obj, gains, sdp_iterations, ms, "gw"};
}

// ── Registry ─────────────────────────────────────────────────

QUBOSolver get_qubo_solver(const std::string& name) {
    if (name == "greedy") return qubo_solve_greedy;
    if (name == "sa") {
        return [](const QUBOInstance& inst, uint64_t seed) {
            return qubo_solve_sa(inst, seed);
        };
    }
    if (name == "gw") {
        return [](const QUBOInstance& inst, uint64_t seed) {
            return qubo_solve_gw(inst, seed);
        };
    }
    return nullptr;
}

std::vector<std::string> list_qubo_solvers() {
    return {"greedy", "sa", "gw"};
}
