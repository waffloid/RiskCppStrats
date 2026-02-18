#include <cassert>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "ai/distribution.hpp"
#include "systems/transport/potential_solvers.hpp"

static bool approx(float a, float b, float tol = 1e-4f) {
    return std::fabs(a - b) < tol;
}

static float sum_weights(const TroopDistribution& d) {
    float s = 0.0f;
    for (float w : d.weights) s += w;
    return s;
}

void test_softmax_uniform() {
    // Equal scores → uniform distribution
    std::vector<float> scores = {1.0f, 1.0f, 1.0f, 1.0f};
    TroopDistribution d = softmax(scores, 1.0f);
    assert(d.weights.size() == 4);
    for (float w : d.weights) assert(approx(w, 0.25f));
    assert(approx(sum_weights(d), 1.0f));
    printf("test_softmax_uniform passed\n");
}

void test_softmax_peaked() {
    // One score much higher → that node gets most weight
    std::vector<float> scores = {0.0f, 0.0f, 10.0f, 0.0f};
    TroopDistribution d = softmax(scores, 1.0f);
    assert(d.weights[2] > 0.9f);
    assert(approx(sum_weights(d), 1.0f));
    printf("test_softmax_peaked passed\n");
}

void test_softmax_high_beta() {
    // High β concentrates distribution
    std::vector<float> scores = {1.0f, 2.0f, 3.0f};
    TroopDistribution lo = softmax(scores, 0.1f);
    TroopDistribution hi = softmax(scores, 10.0f);
    // High beta should be more peaked on node 2
    assert(hi.weights[2] > lo.weights[2]);
    assert(approx(sum_weights(lo), 1.0f));
    assert(approx(sum_weights(hi), 1.0f));
    printf("test_softmax_high_beta passed\n");
}

void test_softmax_empty() {
    std::vector<float> scores;
    TroopDistribution d = softmax(scores, 1.0f);
    assert(d.weights.empty());
    printf("test_softmax_empty passed\n");
}

void test_pool_single() {
    // Pooling a single distribution returns it unchanged
    TroopDistribution d;
    d.weights = {0.5f, 0.3f, 0.2f};
    std::vector<const TroopDistribution*> dists = {&d};
    std::vector<float> weights = {1.0f};
    TroopDistribution result = pool(dists, weights);
    for (int i = 0; i < 3; i++) assert(approx(result.weights[i], d.weights[i]));
    printf("test_pool_single passed\n");
}

void test_pool_equal_weights() {
    // Two distributions with equal weight → average
    TroopDistribution d1, d2;
    d1.weights = {1.0f, 0.0f};
    d2.weights = {0.0f, 1.0f};
    std::vector<const TroopDistribution*> dists = {&d1, &d2};
    std::vector<float> weights = {1.0f, 1.0f};
    TroopDistribution result = pool(dists, weights);
    assert(approx(result.weights[0], 0.5f));
    assert(approx(result.weights[1], 0.5f));
    printf("test_pool_equal_weights passed\n");
}

void test_pool_unequal_weights() {
    TroopDistribution d1, d2;
    d1.weights = {1.0f, 0.0f};
    d2.weights = {0.0f, 1.0f};
    std::vector<const TroopDistribution*> dists = {&d1, &d2};
    std::vector<float> weights = {3.0f, 1.0f};
    TroopDistribution result = pool(dists, weights);
    assert(approx(result.weights[0], 0.75f));
    assert(approx(result.weights[1], 0.25f));
    printf("test_pool_unequal_weights passed\n");
}

void test_ema_first_tick() {
    // Empty prev → returns current
    TroopDistribution current;
    current.weights = {0.5f, 0.3f, 0.2f};
    TroopDistribution prev;  // empty
    TroopDistribution result = ema(current, prev, 0.3f);
    for (int i = 0; i < 3; i++) assert(approx(result.weights[i], current.weights[i]));
    printf("test_ema_first_tick passed\n");
}

void test_ema_blending() {
    TroopDistribution current, prev;
    current.weights = {1.0f, 0.0f};
    prev.weights = {0.0f, 1.0f};
    float alpha = 0.3f;
    TroopDistribution result = ema(current, prev, alpha);
    // Before normalization: [0.3, 0.7], sum = 1.0, so already normalized
    assert(approx(result.weights[0], 0.3f));
    assert(approx(result.weights[1], 0.7f));
    assert(approx(sum_weights(result), 1.0f));
    printf("test_ema_blending passed\n");
}

void test_gradient_deficit() {
    TroopDistribution dist;
    dist.weights = {0.5f, 0.3f, 0.2f};
    std::vector<int> current = {100, 0, 0};  // all troops at node 0
    int total = 100;
    std::vector<float> grad = distribution_to_gradient(dist, current, total);
    // Node 0: wants 50, has 100 → gradient = -50
    assert(approx(grad[0], -50.0f));
    // Node 1: wants 30, has 0 → gradient = +30
    assert(approx(grad[1], 30.0f));
    // Node 2: wants 20, has 0 → gradient = +20
    assert(approx(grad[2], 20.0f));
    printf("test_gradient_deficit passed\n");
}

void test_gradient_balanced() {
    // If troops match distribution, gradient is zero
    TroopDistribution dist;
    dist.weights = {0.5f, 0.5f};
    std::vector<int> current = {50, 50};
    std::vector<float> grad = distribution_to_gradient(dist, current, 100);
    assert(approx(grad[0], 0.0f));
    assert(approx(grad[1], 0.0f));
    printf("test_gradient_balanced passed\n");
}

void test_potential_deficit() {
    // potential_deficit should produce the same result as distribution_to_gradient
    TroopDistribution dist;
    dist.weights = {0.5f, 0.3f, 0.2f};
    std::vector<int> current = {100, 0, 0};
    int total = 100;

    // Use a dummy graph (potential_deficit ignores it for the deficit solver)
    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0;
    Node n1; n1.x = 1; n1.y = 0; n1.idx = 1;
    Node n2; n2.x = 2; n2.y = 0; n2.idx = 2;
    g.nodes = {n0, n1, n2};

    std::vector<float> pot = potential_deficit(g, current, dist, total);
    std::vector<float> grad = distribution_to_gradient(dist, current, total);

    assert(pot.size() == grad.size());
    for (int i = 0; i < 3; i++) {
        assert(approx(pot[i], grad[i]));
    }
    printf("test_potential_deficit passed\n");
}

void test_linear_combine_then_softmax() {
    // Simulate the new pipeline: weighted sum of raw scores → single softmax
    // Two agents: economy (weight=2, scores node 0 at 3.0) and expansion (weight=1, scores node 1 at 1.0)
    int n = 3;
    std::vector<float> economy_scores = {3.0f, 0.0f, 0.0f};
    std::vector<float> expansion_scores = {0.0f, 1.0f, 0.0f};
    float w_econ = 2.0f, w_exp = 1.0f;

    // Linear combine
    std::vector<float> combined(n, 0.0f);
    for (int i = 0; i < n; i++) {
        combined[i] = w_econ * economy_scores[i] + w_exp * expansion_scores[i];
    }
    // combined = [6.0, 1.0, 0.0]

    assert(approx(combined[0], 6.0f));
    assert(approx(combined[1], 1.0f));
    assert(approx(combined[2], 0.0f));

    // Softmax with high beta should heavily concentrate on node 0
    TroopDistribution dist = softmax(combined, 3.0f);
    assert(dist.weights[0] > 0.99f);  // node 0 dominates with score 6 * beta 3
    assert(approx(sum_weights(dist), 1.0f));

    // With low beta, more spread
    TroopDistribution spread = softmax(combined, 0.1f);
    assert(spread.weights[0] > spread.weights[1]);  // node 0 still highest
    assert(spread.weights[0] < 0.7f);  // but not by as much
    assert(approx(sum_weights(spread), 1.0f));

    printf("test_linear_combine_then_softmax passed\n");
}

void test_economy_outbids_expansion() {
    // Verify that higher economy weight + score concentrates on factory candidate
    // even when expansion scores many border nodes
    int n = 10;  // 1 factory candidate + 5 border nodes + 4 empty
    std::vector<float> economy_scores(n, 0.0f);
    std::vector<float> expansion_scores(n, 0.0f);

    economy_scores[0] = 3.0f;  // one good factory candidate
    for (int i = 5; i < 10; i++) expansion_scores[i] = 1.0f;  // 5 border nodes

    // Combine with economy weight=2, expansion weight=1
    std::vector<float> combined(n, 0.0f);
    for (int i = 0; i < n; i++) {
        combined[i] = 2.0f * economy_scores[i] + 1.0f * expansion_scores[i];
    }
    // combined[0] = 6.0, combined[5..9] = 1.0 each, rest = 0.0

    TroopDistribution dist = softmax(combined, 3.0f);

    // Factory candidate should get most weight despite only 1 node vs 5 border nodes
    float border_total = 0.0f;
    for (int i = 5; i < 10; i++) border_total += dist.weights[i];
    assert(dist.weights[0] > border_total);  // factory beats all border nodes combined

    printf("test_economy_outbids_expansion passed\n");
}

int main() {
    test_softmax_uniform();
    test_softmax_peaked();
    test_softmax_high_beta();
    test_softmax_empty();
    test_pool_single();
    test_pool_equal_weights();
    test_pool_unequal_weights();
    test_ema_first_tick();
    test_ema_blending();
    test_gradient_deficit();
    test_gradient_balanced();
    test_potential_deficit();
    test_linear_combine_then_softmax();
    test_economy_outbids_expansion();
    printf("\nAll distribution tests passed.\n");
    return 0;
}
