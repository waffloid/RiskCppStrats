#include <cassert>
#include <cstdio>
#include "engine/combat.hpp"
#include "engine/production.hpp"
#include "engine/routing.hpp"
#include "engine/graph.hpp"

void test_basic_combat() {
    GameConfig config;
    int n_players = 2;

    // Build a trivial graph: one node
    Graph g;
    Node n; n.x = 0; n.y = 0; n.idx = 0;
    g.nodes.push_back(n);

    std::vector<NodeData> all_nodes(1);
    all_nodes[0].troops = {1000, 500};
    all_nodes[0].state = NodeState::DEFAULT;
    all_nodes[0].owner = -1; // contested

    // attack: [1000/100=10, 500/100=5]
    // defense: [1000/1000=1, 500/1000=0]
    // loss_0 = max(5 - 1, 0) = 4
    // loss_1 = max(10 - 0, 0) = 10
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 996);
    assert(all_nodes[0].troops[1] == 490);

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

    // attack: [50/100=0, 30/100=0]  (integer division)
    // No damage dealt — both have zero attack
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 50);
    assert(all_nodes[0].troops[1] == 30);

    printf("test_combat_small_troops passed (no damage below 100)\n");
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

    // attack: [10, 5]
    // defense: [1000/1000=1, 500/1000=0]
    // Fort boosts owner (p0) defense: 1 * 1.2 = 1.2 -> (int)1
    // loss_0 = max(5 - 1, 0) = 4
    // loss_1 = max(10 - 0, 0) = 10
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 996);
    assert(all_nodes[0].troops[1] == 490);

    printf("test_fort_defense passed\n");
}

void test_artillery_attack() {
    GameConfig config;
    int n_players = 2;

    // Two nodes, node 0 and node 1 are neighbors
    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1};
    Node n1; n1.x = 1; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0};
    g.nodes = {n0, n1};

    std::vector<NodeData> all_nodes(2);
    // Node 0: contested
    all_nodes[0].troops = {1000, 500};
    all_nodes[0].state = NodeState::DEFAULT;
    // Node 1: artillery owned by player 0
    all_nodes[1].troops = {100, 0};
    all_nodes[1].state = NodeState::ARTILLERY;
    all_nodes[1].owner = 0;

    // At node 0:
    // base attack: [10, 5]
    // Artillery at nbr (node 1) owned by p0 -> p0 attack *= 1.5 -> 10*1.5=15 -> (int)15
    // defense: [1, 0]
    // loss_0 = max(5 - 1, 0) = 4
    // loss_1 = max(15 - 0, 0) = 15
    resolve_combat(all_nodes[0], 0, g, all_nodes, n_players, config);

    assert(all_nodes[0].troops[0] == 996);
    assert(all_nodes[0].troops[1] == 485);

    printf("test_artillery_attack passed\n");
}

void test_production_basic() {
    GameConfig config;

    // Three nodes: capital, factory, powerplant (all owned by player 0, neighbors of each other)
    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1, 2};
    Node n1; n1.x = 1; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0, 2};
    Node n2; n2.x = 0; n2.y = 1; n2.idx = 2; n2.neighbor_indices = {0, 1};
    g.nodes = {n0, n1, n2};

    std::vector<NodeData> nodes(3);
    nodes[0].state = NodeState::CAPITAL;   nodes[0].owner = 0; nodes[0].troops = {100};
    nodes[1].state = NodeState::FACTORY;   nodes[1].owner = 0; nodes[1].troops = {100};
    nodes[2].state = NodeState::POWERPLANT; nodes[2].owner = 0; nodes[2].troops = {100};

    produce_troops(nodes, g, config);

    // Capital: base 2 + powerplant bonus 2 = 4
    assert(nodes[0].troops[0] == 104);
    // Factory: base 1 + powerplant bonus 2 = 3
    assert(nodes[1].troops[0] == 103);
    // Powerplant: produces nothing itself
    assert(nodes[2].troops[0] == 100);

    printf("test_production_basic passed (capital: %d, factory: %d, pp: %d)\n",
           nodes[0].troops[0], nodes[1].troops[0], nodes[2].troops[0]);
}

void test_routing() {
    // Three nodes in a line: 0 -- 1 -- 2
    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0; n0.neighbor_indices = {1};
    Node n1; n1.x = 5; n1.y = 0; n1.idx = 1; n1.neighbor_indices = {0, 2};
    Node n2; n2.x = 10; n2.y = 0; n2.idx = 2; n2.neighbor_indices = {1};
    g.nodes = {n0, n1, n2};

    // From 0, heading to 2: should pick node 1 (direction is +x)
    assert(next_hop(g, 0, 2) == 1);
    // From 1, heading to 2: should pick node 2
    assert(next_hop(g, 1, 2) == 2);
    // Already at destination
    assert(next_hop(g, 2, 2) == -1);
    // From 2, heading to 0: should pick node 1
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
