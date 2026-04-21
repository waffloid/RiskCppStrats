#include <cassert>
#include <cstdio>
#include "systems/combat/combat_resolver.hpp"
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

    CombatState state;
    state.init(1, n_players);

    // attack: [1000/100=10, 500/100=5]
    // defense: [1000/1000=1.0, 500/1000=0.5]
    // loss_0 = max(5 - 1.0, 0) = 4.0
    // loss_1 = max(10 - 0.5, 0) = 9.5 -> int(9.5) = 9
    NodeCombatResult nr = compute_node_casualties(
        all_nodes[0], 0, g, all_nodes, state.accumulated_damage[0],
        n_players, config, 1.0f);
    all_nodes[0].troops[0] -= nr.casualties[0];
    all_nodes[0].troops[1] -= nr.casualties[1];

    assert(all_nodes[0].troops[0] == 996);
    assert(all_nodes[0].troops[1] == 491);

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

    CombatState state;
    state.init(1, n_players);

    // attack: [50/100=0.5, 30/100=0.3]
    // defense: [50/1000=0.05, 30/1000=0.03]
    // loss_0 per tick = max(0.3 - 0.05, 0) = 0.25
    // loss_1 per tick = max(0.5 - 0.03, 0) = 0.47
    // After 1 tick: no int damage yet, but accumulators are non-zero
    NodeCombatResult nr = compute_node_casualties(
        all_nodes[0], 0, g, all_nodes, state.accumulated_damage[0],
        n_players, config, 1.0f);
    all_nodes[0].troops[0] -= nr.casualties[0];
    all_nodes[0].troops[1] -= nr.casualties[1];
    state.accumulated_damage[0] = nr.updated_damage;

    assert(all_nodes[0].troops[0] == 50);
    assert(all_nodes[0].troops[1] == 30);

    // After 3 ticks total: acc_1 reaches 1.41 -> p1 takes 1 damage
    // acc_0 = 0.75 (not yet 1), so p0 unchanged
    for (int i = 0; i < 2; i++) {
        nr = compute_node_casualties(
            all_nodes[0], 0, g, all_nodes, state.accumulated_damage[0],
            n_players, config, 1.0f);
        all_nodes[0].troops[0] -= nr.casualties[0];
        all_nodes[0].troops[1] -= nr.casualties[1];
        state.accumulated_damage[0] = nr.updated_damage;
    }
    assert(all_nodes[0].troops[0] == 50);
    assert(all_nodes[0].troops[1] == 29);

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

    CombatState state;
    state.init(1, n_players);

    // attack: [1000/100=10, 500/100=5]
    // defense: [1000/1000=1.0, 500/1000=0.5]
    // Fort boosts owner (p0) defense: 1.0 * 1.2 = 1.2
    // loss_0 = max(5 - 1.2, 0) = 3.8 -> int(3.8) = 3
    // loss_1 = max(10 - 0.5, 0) = 9.5 -> int(9.5) = 9
    NodeCombatResult nr = compute_node_casualties(
        all_nodes[0], 0, g, all_nodes, state.accumulated_damage[0],
        n_players, config, 1.0f);
    all_nodes[0].troops[0] -= nr.casualties[0];
    all_nodes[0].troops[1] -= nr.casualties[1];

    assert(all_nodes[0].troops[0] == 997);
    assert(all_nodes[0].troops[1] == 491);

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

    CombatState state;
    state.init(2, n_players);

    // At node 0:
    // base attack: [1000/100=10, 500/100=5]
    // Artillery at nbr (node 1) owned by p0 -> p0 attack *= 1.5 -> 10*1.5=15
    // defense: [1000/1000=1.0, 500/1000=0.5]
    // loss_0 = max(5 - 1.0, 0) = 4.0
    // loss_1 = max(15 - 0.5, 0) = 14.5 -> int(14.5) = 14
    NodeCombatResult nr = compute_node_casualties(
        all_nodes[0], 0, g, all_nodes, state.accumulated_damage[0],
        n_players, config, 1.0f);
    all_nodes[0].troops[0] -= nr.casualties[0];
    all_nodes[0].troops[1] -= nr.casualties[1];

    assert(all_nodes[0].troops[0] == 996);
    assert(all_nodes[0].troops[1] == 486);

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

void test_resolve_all_pure() {
    GameConfig config;
    int n_players = 2;

    Graph g;
    Node n0; n0.x = 0; n0.y = 0; n0.idx = 0;
    g.nodes.push_back(n0);

    std::vector<NodeData> nodes(1);
    nodes[0].troops = {1000, 500};
    nodes[0].state = NodeState::DEFAULT;
    nodes[0].owner = -1;

    CombatState state;
    state.init(1, n_players);

    AllCombatResults results = resolve_all_node_combat(
        g, nodes, state, n_players, config, 1.0f);

    assert(results.per_node.size() == 1);
    assert(results.total_deaths[0] == 4);
    assert(results.total_deaths[1] == 9);
    // Verify original nodes are NOT mutated (pure function)
    assert(nodes[0].troops[0] == 1000);
    assert(nodes[0].troops[1] == 500);

    printf("test_resolve_all_pure passed (deaths: %d, %d)\n",
           results.total_deaths[0], results.total_deaths[1]);
}

int main() {
    test_basic_combat();
    test_combat_small_troops();
    test_fort_defense();
    test_artillery_attack();
    test_production_basic();
    test_routing();
    test_resolve_all_pure();
    printf("All combat/production/routing tests passed\n");
    return 0;
}
