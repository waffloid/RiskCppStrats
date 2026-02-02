#include <cassert>
#include <cstdio>
#include <cmath>
#include "engine/game.hpp"

static GameConfig small_config() {
    GameConfig c;
    c.poisson_intensity = 0.01f;
    c.region_width = 50.0f;
    c.region_height = 50.0f;
    c.edge_distance_threshold = 15.0f;
    c.max_neighbors = 7;
    return c;
}

static void tick_empty(Game& game, int n_ticks, int n_players) {
    std::vector<PlayerCommands> cmds(n_players);
    for (int i = 0; i < n_ticks; i++) {
        game.tick(1.0f, cmds);
    }
}

// Find a neighbor of node_idx that is not a capital (not node 0 or 1)
static int find_non_capital_neighbor(const Game& game, int node_idx) {
    for (int nb : game.graph().nodes[node_idx].neighbor_indices) {
        if (game.node_data()[nb].state != NodeState::CAPITAL) return nb;
    }
    return -1;
}

void test_ownership_transfer() {
    GameConfig config = small_config();
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_ownership_transfer skipped (no non-capital neighbor)\n");
        return;
    }

    // Initially unowned
    assert(game.node_data()[target].owner == -1);

    // Send troops
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, 100});
    game.tick(1.0f, cmds);

    // Wait for arrival
    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[0] > 0) break;
    }
    assert(game.node_data()[target].troops[0] > 0);
    assert(game.node_data()[target].owner == 0);

    printf("test_ownership_transfer passed (target owner: %d, troops: %d)\n",
           game.node_data()[target].owner, game.node_data()[target].troops[0]);
}

void test_ownership_contested() {
    GameConfig config = small_config();
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_ownership_contested skipped\n");
        return;
    }

    // Send p0 troops
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, 200});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[0] > 0) break;
    }
    assert(game.node_data()[target].owner == 0);

    // Send p1 troops to same target
    cmds = std::vector<PlayerCommands>(2);
    cmds[1].troops.push_back({1, target, 200});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[1] > 0) break;
    }

    if (game.node_data()[target].troops[0] > 0 && game.node_data()[target].troops[1] > 0) {
        assert(game.node_data()[target].owner == -1);
        printf("test_ownership_contested passed (contested)\n");
    } else {
        printf("test_ownership_contested passed (combat resolved before check)\n");
    }
}

void test_combat_kills_troops() {
    GameConfig config = small_config();
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_combat_kills_troops skipped\n");
        return;
    }

    // Send p0 troops
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, 300});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[0] > 0) break;
    }
    int p0_at_target = game.node_data()[target].troops[0];
    assert(p0_at_target > 0);

    // Send p1 troops
    cmds = std::vector<PlayerCommands>(2);
    cmds[1].troops.push_back({1, target, 300});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[1] > 0) break;
    }

    int p0_before = game.node_data()[target].troops[0];
    int p1_before = game.node_data()[target].troops[1];

    // Both present — run combat ticks
    tick_empty(game, 200, 2);

    int p0_after = game.node_data()[target].troops[0];
    int p1_after = game.node_data()[target].troops[1];

    bool combat_happened = (p0_after < p0_before) || (p1_after < p1_before)
                        || (p0_after == 0) || (p1_after == 0);
    assert(combat_happened);

    printf("test_combat_kills_troops passed (p0: %d->%d, p1: %d->%d)\n",
           p0_before, p0_after, p1_before, p1_after);
}

void test_neutral_troops() {
    GameConfig config = small_config();
    config.init_default_troops = 25;
    Game game(config, {0, 1}, 42);

    int neutral_id = 2;
    assert(game.n_players() == 3);

    int neutral_nodes = 0;
    for (int i = 0; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        if (nd.state != NodeState::CAPITAL) {
            assert(nd.troops[neutral_id] == 25);
            assert(nd.owner == neutral_id);
            neutral_nodes++;
        }
    }
    assert(neutral_nodes > 0);

    assert(game.node_data()[0].troops[neutral_id] == 0);
    assert(game.node_data()[1].troops[neutral_id] == 0);

    printf("test_neutral_troops passed (%d neutral nodes)\n", neutral_nodes);
}

void test_capture_neutral_node() {
    GameConfig config = small_config();
    config.init_default_troops = 25;
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_capture_neutral_node skipped\n");
        return;
    }
    int neutral_id = 2;

    assert(game.node_data()[target].owner == neutral_id);

    // Send 400 troops to overwhelm 25 defenders
    std::vector<PlayerCommands> cmds(3);
    cmds[0].troops.push_back({0, target, 400});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 1000; i++) {
        tick_empty(game, 1, 3);
        if (game.node_data()[target].owner == 0 && game.node_data()[target].troops[neutral_id] == 0) {
            break;
        }
    }

    assert(game.node_data()[target].owner == 0);
    assert(game.node_data()[target].troops[neutral_id] == 0);
    assert(game.node_data()[target].troops[0] > 0);

    printf("test_capture_neutral_node passed (p0 captured with %d troops)\n",
           game.node_data()[target].troops[0]);
}

void test_build_factory_on_owned_node() {
    GameConfig config = small_config();
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_build_factory_on_owned_node skipped\n");
        return;
    }

    // Send all starting troops
    int send_amount = config.init_troop_count;
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, send_amount});
    game.tick(1.0f, cmds);

    // Need cost + 1 troops to build (must keep 1 to hold ownership)
    int needed = config.cost_factory + 1;
    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[0] >= needed) break;
    }
    assert(game.node_data()[target].owner == 0);
    assert(game.node_data()[target].troops[0] >= needed);

    // Build factory
    cmds = std::vector<PlayerCommands>(2);
    cmds[0].builds.push_back({target, NodeState::FACTORY});
    int troops_before = game.node_data()[target].troops[0];
    game.tick(1.0f, cmds);

    assert(game.node_data()[target].state == NodeState::FACTORY);
    assert(game.node_data()[target].troops[0] >= troops_before - config.cost_factory);
    assert(game.node_data()[target].troops[0] >= 1);  // kept at least 1

    printf("test_build_factory_on_owned_node passed (factory built, %d troops left)\n",
           game.node_data()[target].troops[0]);
}

void test_factory_produces() {
    GameConfig config = small_config();
    config.cost_factory = 100;  // cheaper factory so troops remain after building
    Game game(config, {0, 1}, 42);

    int target = find_non_capital_neighbor(game, 0);
    if (target < 0) {
        printf("test_factory_produces skipped\n");
        return;
    }

    // Send troops, keep some for garrison
    std::vector<PlayerCommands> cmds(2);
    cmds[0].troops.push_back({0, target, 300});
    game.tick(1.0f, cmds);

    for (int i = 0; i < 500; i++) {
        tick_empty(game, 1, 2);
        if (game.node_data()[target].troops[0] >= config.cost_factory + 2) break;
    }
    assert(game.node_data()[target].owner == 0);

    // Build factory
    cmds = std::vector<PlayerCommands>(2);
    cmds[0].builds.push_back({target, NodeState::FACTORY});
    game.tick(1.0f, cmds);
    assert(game.node_data()[target].state == NodeState::FACTORY);
    assert(game.node_data()[target].owner == 0);

    int troops_after_build = game.node_data()[target].troops[0];
    assert(troops_after_build > 0);

    // Run 10 ticks — factory should produce
    tick_empty(game, 10, 2);

    int troops_after = game.node_data()[target].troops[0];
    int min_expected = troops_after_build + 10 * config.factory_troops_per_tick;
    assert(troops_after >= min_expected);

    printf("test_factory_produces passed (%d -> %d, expected >= %d)\n",
           troops_after_build, troops_after, min_expected);
}

void test_ownership_updates_every_tick() {
    GameConfig config = small_config();
    Game game(config, {0, 1}, 42);

    assert(game.node_data()[0].owner == 0);
    assert(game.node_data()[1].owner == 1);

    tick_empty(game, 1, 2);
    assert(game.node_data()[0].owner == 0);
    assert(game.node_data()[1].owner == 1);

    // Non-capital empty nodes should be unowned
    for (int i = 2; i < game.graph().num_nodes(); i++) {
        const auto& nd = game.node_data()[i];
        int total = 0;
        for (int t : nd.troops) total += t;
        if (total == 0) {
            assert(nd.owner == -1);
        }
    }

    printf("test_ownership_updates_every_tick passed\n");
}

int main() {
    test_ownership_transfer();
    test_ownership_contested();
    test_combat_kills_troops();
    test_neutral_troops();
    test_capture_neutral_node();
    test_build_factory_on_owned_node();
    test_factory_produces();
    test_ownership_updates_every_tick();
    printf("All gameplay tests passed\n");
    return 0;
}
