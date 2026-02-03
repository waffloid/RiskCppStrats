#include <cassert>
#include <cstdio>
#include "engine/combat.hpp"
#include "engine/production.hpp"
#include "engine/routing.hpp"
#include "engine/graph.hpp"

void test_basic_combat() {
    GameConfig config;
    int n_players = 2;

    Graph g;
    Node n; n.x = 0; n.y = 0; n.idx = 0;
    g.nodes.push_back(n);

    std::vector<NodeData> all_nodes(1);
    all_nodes[0].troops = {1000, 500};
    all_nodes[0].state = NodeState::DEFAULT;
    all_nodes[0].owner = -1;

    // attack: [1000/10=100, 500/10=50]
    // defense: [1000/100=10, 500/100=5]
    // loss_0 = max(50 - 10, 0) = 40
    // loss_1 = max(100 - 5, 0) = 95
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 960);
    assert(all_nodes[0].troops[1] == 405);

    printf("test_basic_combat passed (troops: %d, %d)\n",
           all_nodes[0].troops[0], all_nodes[0].troops[1]);
}

void test_combat_small_troops() {
    GameConfig config;
    int n_players = 2;

    Graph g;
    Node n; n.x = 0; n.y = 0; n.idx = 0;
    g.nodes.push_back(n);

    std::vector<NodeData> all_nodes(1);
    all_nodes[0].troops = {50, 30};
    all_nodes[0].state = NodeState::DEFAULT;

    // attack: [50/10=5, 30/10=3]
    // defense: [50/100=0, 30/100=0]
    // loss_0 = max(3 - 0, 0) = 3
    // loss_1 = max(5 - 0, 0) = 5
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 47);
    assert(all_nodes[0].troops[1] == 25);

    printf("test_combat_small_troops passed (troops: %d, %d)\n",
           all_nodes[0].troops[0], all_nodes[0].troops[1]);
}

void test_fort_defense() {
    GameConfig config;
    int n_players = 2;

    Graph g;
    Node n; n.x = 0; n.y = 0; n.idx = 0;
    g.nodes.push_back(n);

    std::vector<NodeData> all_nodes(1);
    all_nodes[0].troops = {1000, 500};
    all_nodes[0].state = NodeState::FORT;
    all_nodes[0].owner = 0;

    // attack: [100, 50]
    // defense: [1000/100=10, 500/100=5]
    // Fort boosts owner (p0) defense: 10 * 1.2 = 12 -> (int)12
    // loss_0 = max(50 - 12, 0) = 38
    // loss_1 = max(100 - 5, 0) = 95
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 962);
    assert(all_nodes[0].troops[1] == 405);

    printf("test_fort_defense passed (troops: %d, %d)\n",
           all_nodes[0].troops[0], all_nodes[0].troops[1]);
}

void test_artillery_attack() {
    GameConfig config;
    int n_players = 2;

    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1};
    Node n1; n1.x = 1; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0};
    g.nodes = {n0, n1};

    std::vector<NodeData> all_nodes(2);
    all_nodes[0].troops = {1000, 500};
    all_nodes[0].state = NodeState::DEFAULT;
    all_nodes[1].troops = {100, 0};
    all_nodes[1].state = NodeState::ARTILLERY;
    all_nodes[1].owner = 0;

    // At node 0:
    // base attack: [100, 50]
    // Artillery at nbr (node 1) owned by p0 -> p0 attack *= 1.5 -> 100*1.5=150
    // defense: [10, 5]
    // loss_0 = max(50 - 10, 0) = 40
    // loss_1 = max(150 - 5, 0) = 145
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 960);
    assert(all_nodes[0].troops[1] == 355);

    printf("test_artillery_attack passed (troops: %d, %d)\n",
           all_nodes[0].troops[0], all_nodes[0].troops[1]);
}

void test_production_basic() {
    GameConfig config;

    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1, 2};
    Node n1; n1.x = 1; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0, 2};
    Node n2; n2.x = 0; n2.y = 1; n2.idx = 2; n2.neighbor_indices = {0, 1};
    g.nodes = {n0, n1, n2};

    std::vector<NodeData> nodes(3);
    nodes[0].state = NodeState::CAPITAL;   nodes[0].owner = 0; nodes[0].troops = {100};
    nodes[1].state = NodeState::FACTORY;   nodes[1].owner = 0; nodes[1].troops = {100};
    nodes[2].state = NodeState::POWERPLANT; nodes[2].owner = 0; nodes[2].troops = {100};

    produce_troops(nodes, g, config);  // always produces 1 tick regardless of game speed

    assert(nodes[0].troops[0] == 104);
    assert(nodes[1].troops[0] == 103);
    assert(nodes[2].troops[0] == 100);

    printf("test_production_basic passed (capital: %d, factory: %d, pp: %d)\n",
           nodes[0].troops[0], nodes[1].troops[0], nodes[2].troops[0]);
}

void test_routing() {
    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1};
    Node n1; n1.x = 5; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0, 2};
    Node n2; n2.x = 10; n2.y = 0; n2.idx = 2; n2.neighbor_indices = {1};
    g.nodes = {n0, n1, n2};

    assert(next_hop(g, 0, 2) == 1);
    assert(next_hop(g, 1, 2) == 2);
    assert(next_hop(g, 2, 2) == -1);
    assert(next_hop(g, 2, 0) == 1);

    printf("test_routing passed\n");
}

int main() {
    test_basic_combat();
    test_combat_small_troops();
    test_fort_defense();
    test_artillery_attack();
    test_production_basic();
    test_routing();
    printf("All combat/production/routing tests passed\n");
    return 0;
}
