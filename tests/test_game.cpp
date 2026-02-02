#include <cassert>
#include <cstdio>
#include "engine/game.hpp"

void test_game_construction() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 6;

    // Generate graph first to know how many nodes we get
    Graph g = Graph::generate_poisson(config, 42);
    assert(g.num_nodes() >= 2);

    Game game(config, {0, 1}, 42);

    assert(game.n_players() == 2);
    assert(game.is_alive(0));
    assert(game.is_alive(1));
    assert(!game.is_game_over());

    // Capitals should have initial troops
    assert(game.node_data()[0].troops[0] == config.init_troop_count);
    assert(game.node_data()[1].troops[1] == config.init_troop_count);
    assert(game.node_data()[0].state == NodeState::CAPITAL);
    assert(game.node_data()[1].state == NodeState::CAPITAL);

    printf("test_game_construction passed (nodes: %d, edges: %d)\n",
           game.graph().num_nodes(), game.graph().num_edges());
}

void test_empty_ticks() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;

    Game game(config, {0, 1}, 42);

    // Run several ticks with no commands — just production
    std::vector<PlayerCommands> empty_cmds(2);
    int initial_p0 = game.node_data()[0].troops[0];

    for (int i = 0; i < 10; i++) {
        game.tick(1.0f, empty_cmds);
    }

    // Capital produces troops each tick
    int expected = initial_p0 + 10 * config.capital_troops_per_tick;
    assert(game.node_data()[0].troops[0] == expected);

    printf("test_empty_ticks passed (p0 troops after 10 ticks: %d)\n",
           game.node_data()[0].troops[0]);
}

void test_build_factory() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;

    Game game(config, {0, 1}, 42);

    // Find a neighbor of node 0 that we can build on
    const auto& nbrs = game.graph().nodes[0].neighbor_indices;
    if (nbrs.empty()) {
        printf("test_build_factory skipped (node 0 has no neighbors)\n");
        return;
    }

    int target = nbrs[0];
    // First, we need troops at that node. Put some there manually via the game:
    // We'll send troops from node 0 to the neighbor.
    // But the neighbor might not be adjacent to our destination. Let's just test
    // building at a node we own. We need to give ownership first.

    // Actually, you can only build on nodes you own. Let's give player 0 troops
    // at the neighbor by running enough ticks and sending.
    // For simplicity, let's test the validation: build should fail if we don't own the node.
    std::vector<PlayerCommands> cmds(2);
    cmds[0].builds.push_back({target, NodeState::FACTORY});
    game.tick(1.0f, cmds);

    // Should fail — player 0 doesn't own the target node
    assert(game.node_data()[target].state == NodeState::DEFAULT);

    printf("test_build_factory passed (correctly rejected build on unowned node)\n");
}

void test_troop_send_and_arrive() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;

    Game game(config, {0, 1}, 42);

    const auto& nbrs = game.graph().nodes[0].neighbor_indices;
    if (nbrs.empty()) {
        printf("test_troop_send_and_arrive skipped (no neighbors)\n");
        return;
    }

    int target = nbrs[0];
    int send_count = 100;
    int initial = game.node_data()[0].troops[0];

    // Send troops
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, send_count});
    game.tick(1.0f, cmds);

    // Troops should have left node 0
    assert(game.node_data()[0].troops[0] == initial - send_count + config.capital_troops_per_tick);

    // Tick until troops arrive (they should eventually)
    std::vector<PlayerCommands> empty(2);
    bool arrived = false;
    for (int i = 0; i < 500; i++) {
        game.tick(1.0f, empty);
        if (game.node_data()[target].troops[0] > 0) {
            arrived = true;
            break;
        }
    }

    assert(arrived);
    printf("test_troop_send_and_arrive passed (troops at target: %d)\n",
           game.node_data()[target].troops[0]);
}

void test_deterministic() {
    GameConfig config;
    config.poisson_intensity = 0.01f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;

    Game g1(config, {0, 1}, 42);
    Game g2(config, {0, 1}, 42);

    std::vector<PlayerCommands> cmds(2);
    if (!g1.graph().nodes[0].neighbor_indices.empty()) {
        int nbr = g1.graph().nodes[0].neighbor_indices[0];
        cmds[0].troops.push_back({0, nbr, 50});
    }

    for (int i = 0; i < 100; i++) {
        g1.tick(1.0f, cmds);
        g2.tick(1.0f, cmds);
        // Only send on first tick
        cmds[0].troops.clear();
    }

    // Both games should be in identical state
    for (int i = 0; i < g1.graph().num_nodes(); i++) {
        for (int p = 0; p < 2; p++) {
            assert(g1.node_data()[i].troops[p] == g2.node_data()[i].troops[p]);
        }
    }

    printf("test_deterministic passed\n");
}

int main() {
    test_game_construction();
    test_empty_ticks();
    test_build_factory();
    test_troop_send_and_arrive();
    test_deterministic();
    printf("All game tests passed\n");
    return 0;
}
