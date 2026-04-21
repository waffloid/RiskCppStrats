#include <cassert>
#include <cstdio>
#include "engine/graph.hpp"

void test_poisson_generation() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 6;

    Graph g = Graph::generate_poisson(config, 42);

    printf("nodes: %d, edges: %d\n", g.num_nodes(), g.num_edges());
    assert(g.num_nodes() > 0);
    assert(g.num_edges() > 0);

    // All node degrees should respect max_neighbors
    for (int i = 0; i < g.num_nodes(); i++) {
        assert(g.degree(i) <= config.max_neighbors);
    }

    // All edges should be within distance threshold
    for (const auto& e : g.edges) {
        assert(e.length <= config.edge_distance_threshold + 0.001f);
        assert(e.a_idx < e.b_idx);
    }

    // Edge index should be consistent
    for (const auto& e : g.edges) {
        int found = g.edge_between(e.a_idx, e.b_idx);
        assert(found == e.idx);
    }

    // Non-existent edges return -1
    // (pick two nodes far apart — nodes 0 and last, likely far)
    // This may or may not be an edge, so just test the lookup doesn't crash
    g.edge_between(0, g.num_nodes() - 1);

    printf("test_poisson_generation passed\n");
}

void test_subgraph() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 6;

    Graph g = Graph::generate_poisson(config, 42);
    assert(g.num_nodes() >= 5);

    std::vector<int> subset = {0, 1, 2, 3, 4};
    Graph sg = g.subgraph(subset);

    assert(sg.num_nodes() == 5);
    // Subgraph nodes should have indices 0..4
    for (int i = 0; i < sg.num_nodes(); i++) {
        assert(sg.nodes[i].idx == i);
    }
    // All subgraph edges should reference valid nodes
    for (const auto& e : sg.edges) {
        assert(e.a_idx >= 0 && e.a_idx < sg.num_nodes());
        assert(e.b_idx >= 0 && e.b_idx < sg.num_nodes());
    }

    printf("test_subgraph passed\n");
}

void test_deterministic() {
    GameConfig config;
    Graph g1 = Graph::generate_poisson(config, 123);
    Graph g2 = Graph::generate_poisson(config, 123);

    assert(g1.num_nodes() == g2.num_nodes());
    assert(g1.num_edges() == g2.num_edges());
    for (int i = 0; i < g1.num_nodes(); i++) {
        assert(g1.nodes[i].x == g2.nodes[i].x);
        assert(g1.nodes[i].y == g2.nodes[i].y);
    }

    printf("test_deterministic passed\n");
}

int main() {
    test_poisson_generation();
    test_subgraph();
    test_deterministic();
    printf("All graph tests passed\n");
    return 0;
}
