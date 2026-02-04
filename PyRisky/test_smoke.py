"""
Smoke test for PyRisky game engine.

Tests basic functionality of Graph and RiskyGraph classes.
"""

import numpy as np
from graph import Graph, induced_graph, perturbed_grid, poisson_graph
from risky_graph import (
    RiskyGraph, NodeState, TravelingTroops,
    STARTING_TROOPS, FACTORY_GENERATION, CAPITAL_GENERATION, POWERPLANT_BONUS
)


def test_graph_basics():
    """Test basic Graph operations."""
    print("Testing Graph basics...")

    # Create a simple triangle graph
    positions = np.array([
        [0.0, 0.0],
        [0.5, 0.0],
        [0.25, 0.4]
    ])
    neighbors = [
        {1, 2},  # Node 0 connects to 1 and 2
        {0, 2},  # Node 1 connects to 0 and 2
        {0, 1}   # Node 2 connects to 0 and 1
    ]
    g = Graph(positions, neighbors)

    assert g.num_nodes == 3
    assert g.are_neighbors(0, 1)
    assert g.are_neighbors(1, 2)
    assert not g.are_neighbors(0, 0)  # No self-loops
    assert set(g.neighbors(0)) == {1, 2}
    assert g.is_connected()

    # Test distance
    dist_01 = g.distance(0, 1)
    assert abs(dist_01 - 0.5) < 1e-9

    # Test subgraph
    sub = g.subgraph([0, 1])
    assert sub.num_nodes == 2
    assert sub.are_neighbors(0, 1)

    print("  Graph basics: PASSED")


def test_induced_graph():
    """Test induced graph construction."""
    print("Testing induced graph...")

    positions = np.array([
        [0.0, 0.0],
        [0.5, 0.0],
        [2.0, 0.0]  # Too far from others
    ])
    g = induced_graph(positions, threshold=1.0)

    assert g.are_neighbors(0, 1)
    assert not g.are_neighbors(0, 2)  # Distance > threshold
    assert not g.are_neighbors(1, 2)

    print("  Induced graph: PASSED")


def test_perturbed_grid():
    """Test perturbed grid generation."""
    print("Testing perturbed grid...")

    rng = np.random.default_rng(42)
    g = perturbed_grid(n=3, m=3, k=1.0, r=0.1, threshold=1.5, rng=rng)

    assert g.num_nodes == 9
    assert g.is_connected()

    print("  Perturbed grid: PASSED")


def test_poisson_graph():
    """Test Poisson process graph generation."""
    print("Testing Poisson graph...")

    rng = np.random.default_rng(42)
    g = poisson_graph(
        width=5.0, height=5.0, intensity=2.0,
        threshold=1.5, max_degree=7, rng=rng
    )

    assert g.num_nodes > 0
    assert g.is_connected()

    print("  Poisson graph: PASSED")


def test_risky_graph_init():
    """Test RiskyGraph initialization."""
    print("Testing RiskyGraph init...")

    g = perturbed_grid(n=5, m=5, k=1.0, r=0.1, threshold=1.5, rng=np.random.default_rng(42))
    rg = RiskyGraph(g, capitals=[0, 24])  # Corners

    assert rg.num_players == 2
    assert rg.player_alive[0] and rg.player_alive[1]
    assert rg.get_node_owner(0) == 0
    assert rg.get_node_owner(24) == 1
    assert rg.get_node_owner(12) is None  # Middle node, unowned

    # Check starting troops
    troops_0 = rg.get_node_troops(0)
    assert troops_0[0] == STARTING_TROOPS
    assert troops_0[1] == 0

    print("  RiskyGraph init: PASSED")


def test_troop_movement():
    """Test sending and moving troops."""
    print("Testing troop movement...")

    g = perturbed_grid(n=3, m=3, k=1.0, r=0.0, threshold=1.5, rng=np.random.default_rng(42))
    rg = RiskyGraph(g, capitals=[0, 8])

    # Send troops from capital 0 toward center
    initial_troops = rg.get_node_troops(0)[0]
    sent = rg.send_troops(from_node=0, to_node=4, player=0, num_troops=50)
    assert sent

    # Troops should be deducted
    remaining = rg.get_node_troops(0)[0]
    assert remaining == initial_troops - 50

    # Should have traveling troops
    traveling = rg.get_traveling_troops(player=0)
    assert len(traveling) == 1
    assert traveling[0].num_troops == 50
    assert traveling[0].global_to_node == 4

    print("  Troop movement: PASSED")


def test_troop_arrival():
    """Test troops arriving at destination."""
    print("Testing troop arrival...")

    # Simple line graph for predictable pathing
    positions = np.array([[0, 0], [1, 0], [2, 0]])
    neighbors = [{1}, {0, 2}, {1}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 2])

    # Clear garrison at node 1 so we can test pure arrival
    rg._garrison[1] = 0

    # Send from 0 to 1 (neighbor)
    rg.send_troops(0, 1, player=0, num_troops=50)

    # Travel time = max(dist * sqrt(troops) * Q, MIN) = max(1 * sqrt(50) * 5, 10) ≈ 36 ticks
    # Run enough ticks for arrival
    for _ in range(50):
        rg.update()
        if rg.get_node_troops(1)[0] > 0:
            break

    assert rg.get_node_troops(1)[0] == 50, "Troops should have arrived"
    assert rg.get_node_owner(1) == 0

    print("  Troop arrival: PASSED")


def test_combat():
    """Test combat resolution."""
    print("Testing combat...")

    positions = np.array([[0, 0], [1, 0]])
    neighbors = [{1}, {0}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 1])

    # Manually set up contested node
    rg._node_troops[0, 0] = 100
    rg._node_troops[0, 1] = 100  # Both players at node 0

    initial_p0 = rg._node_troops[0, 0]
    initial_p1 = rg._node_troops[0, 1]

    rg._update_battles()

    # Both should have lost troops
    assert rg._node_troops[0, 0] < initial_p0
    assert rg._node_troops[0, 1] < initial_p1

    print("  Combat: PASSED")


def test_factory_generation():
    """Test troop generation from factories."""
    print("Testing factory generation...")

    positions = np.array([[0, 0], [1, 0], [2, 0]])
    neighbors = [{1}, {0, 2}, {1}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 2])

    # Give player 0 node 1 and make it a factory
    rg._garrison[1] = 0  # Clear garrison so player can own it
    rg._node_troops[1, 0] = 1000
    rg._last_owner[1] = 0
    rg.set_node_state(1, NodeState.FACTORY, player=0)

    # Read values AFTER building (cost already deducted)
    initial_at_capital = rg._node_troops[0, 0]
    initial_at_factory = rg._node_troops[1, 0]

    rg._update_factories()

    # Capital should have gained CAPITAL_GENERATION
    assert rg._node_troops[0, 0] == initial_at_capital + CAPITAL_GENERATION

    # Factory should have gained FACTORY_GENERATION
    assert rg._node_troops[1, 0] == initial_at_factory + FACTORY_GENERATION

    print("  Factory generation: PASSED")


def test_powerplant_stacking():
    """Test that powerplant bonuses stack."""
    print("Testing powerplant stacking...")

    # Create a star: center node surrounded by 3 others
    positions = np.array([[0, 0], [1, 0], [0, 1], [-1, 0]])
    neighbors = [{1, 2, 3}, {0}, {0}, {0}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 1])

    # Give player 0 all nodes with lots of troops
    for node in [0, 2, 3]:
        rg._garrison[node] = 0  # Clear garrison so player can own it
        rg._node_troops[node, 0] = 10000
        rg._last_owner[node] = 0

    # Make nodes 2 and 3 powerplants
    rg.set_node_state(2, NodeState.POWERPLANT, player=0)
    rg.set_node_state(3, NodeState.POWERPLANT, player=0)

    initial = rg._node_troops[0, 0]
    rg._update_factories()

    # Capital should get CAPITAL_GENERATION + 2 * POWERPLANT_BONUS (two adjacent powerplants)
    expected = initial + CAPITAL_GENERATION + 2 * POWERPLANT_BONUS
    assert rg._node_troops[0, 0] == expected, f"Expected {expected}, got {rg._node_troops[0, 0]}"

    print("  Powerplant stacking: PASSED")


def test_player_elimination():
    """Test player elimination destroys traveling troops."""
    print("Testing player elimination...")

    positions = np.array([[0, 0], [1, 0], [2, 0]])
    neighbors = [{1}, {0, 2}, {1}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 2])

    # Send troops from player 0
    rg.send_troops(0, 1, player=0, num_troops=50)
    assert len(rg.get_traveling_troops(player=0)) == 1

    # Wipe out player 0's capital
    rg._node_troops[0, 0] = 0

    rg._update_player_status()

    assert not rg.player_alive[0]
    assert len(rg.get_traveling_troops(player=0)) == 0  # Troops destroyed

    print("  Player elimination: PASSED")


def test_fort_bonus():
    """Test fort defense bonus goes to last genuine owner."""
    print("Testing fort bonus...")

    positions = np.array([[0, 0], [1, 0]])
    neighbors = [{1}, {0}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 1])

    # Player 0 builds a fort at node 0
    rg.set_node_state(0, NodeState.FORT, player=0)

    # Now player 1 invades with troops
    rg._node_troops[0, 1] = 100

    # Player 0 should still get fort bonus (they're last genuine owner)
    assert rg._last_owner[0] == 0

    print("  Fort bonus: PASSED")


def test_spatial_queries():
    """Test spatial query methods."""
    print("Testing spatial queries...")

    g = perturbed_grid(n=3, m=3, k=1.0, r=0.0, threshold=1.5, rng=np.random.default_rng(42))
    rg = RiskyGraph(g, capitals=[0, 8])

    # Get nodes in radius around center
    center = (1.0, 1.0)
    nodes = rg.get_nodes_in_radius(center, radius=0.6)
    assert 4 in nodes  # Center node should be included

    # Test bulk send
    rg._node_troops[0, 0] = 1000
    rg._node_troops[1, 0] = 1000
    rg._node_troops[3, 0] = 1000
    rg._last_owner[1] = 0
    rg._last_owner[3] = 0

    selected = {0, 1, 3}
    total_sent = rg.send_from_nodes(selected, player=0, target_node=4, amount=50, is_percentage=True)

    assert total_sent > 0

    print("  Spatial queries: PASSED")


def test_retreat():
    """Test troop retreat."""
    print("Testing retreat...")

    positions = np.array([[0, 0], [1, 0], [2, 0]])
    neighbors = [{1}, {0, 2}, {1}]
    g = Graph(positions, neighbors)
    rg = RiskyGraph(g, capitals=[0, 2])

    # Send troops
    rg.send_troops(0, 2, player=0, num_troops=50)
    traveling = rg.get_traveling_troops(player=0)
    assert len(traveling) == 1

    # Advance a bit
    rg.current_tick = 0.5

    # Retreat
    rg.retreat_troops(traveling[0])

    # Should now have troops going back
    new_traveling = rg.get_traveling_troops(player=0)
    assert len(new_traveling) == 1
    assert new_traveling[0].global_to_node == 0  # Going back to origin

    print("  Retreat: PASSED")


def test_full_game_loop():
    """Test a few ticks of the full game loop."""
    print("Testing full game loop...")

    g = perturbed_grid(n=5, m=5, k=1.0, r=0.1, threshold=1.5, rng=np.random.default_rng(42))
    rg = RiskyGraph(g, capitals=[0, 24])

    # Run 100 ticks
    for tick in range(100):
        rg.update()

        # Players should still be alive (no combat yet)
        assert rg.player_alive[0]
        assert rg.player_alive[1]

        # Troops should be accumulating at capitals
        if tick > 0:
            assert rg._node_troops[0, 0] >= STARTING_TROOPS
            assert rg._node_troops[24, 1] >= STARTING_TROOPS

    print("  Full game loop: PASSED")


def main():
    print("=" * 60)
    print("PyRisky Smoke Test")
    print("=" * 60)
    print()

    test_graph_basics()
    test_induced_graph()
    test_perturbed_grid()
    test_poisson_graph()
    test_risky_graph_init()
    test_troop_movement()
    test_troop_arrival()
    test_combat()
    test_factory_generation()
    test_powerplant_stacking()
    test_player_elimination()
    test_fort_bonus()
    test_spatial_queries()
    test_retreat()
    test_full_game_loop()

    print()
    print("=" * 60)
    print("ALL TESTS PASSED")
    print("=" * 60)


if __name__ == "__main__":
    main()
