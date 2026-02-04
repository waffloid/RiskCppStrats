"""
Demo script for Risky Strats.

Runs a game between two simple AIs and visualizes it.
"""

import numpy as np
from graph import perturbed_grid
from graph import poisson_graph
from risky_graph import RiskyGraph
from simple_ai import SimpleAI
from attention_ai import AttentionAI
from visualizer import GameVisualizer


def run_demo(
    grid_size: tuple[int, int] = (8, 8),
    seed: int = 42,
    ai_interval: int = 10,
    max_ticks: int = 5000
):
    """
    Run a demo game.

    Args:
        grid_size: (rows, cols) for the game grid.
        seed: Random seed for reproducibility.
        ai_interval: How often AIs take actions (every N ticks).
        max_ticks: Maximum game length.
    """
    print("Initializing game...")

    # Create graph
    rng = np.random.default_rng(seed)
    n, m = grid_size
    graph = perturbed_grid(
        n=n, m=m,
        k=1.0,           # Grid spacing
        r=0.15,          # Perturbation
        threshold=1.5,   # Edge threshold
        rng=rng
    )
    print(f"  Graph: {graph.num_nodes} nodes")

    # Place capitals at opposite corners
    # Node indices in a grid: row i, col j -> i * m + j
    capital_p0 = 0                    # Top-left
    capital_p1 = (n - 1) * m + (m - 1)  # Bottom-right

    # Create game
    game = RiskyGraph(graph, capitals=[capital_p0, capital_p1])
    print(f"  Players: {game.num_players}")
    print(f"  Capitals: P0 at node {capital_p0}, P1 at node {capital_p1}")

    # Create AIs with slightly different personalities
    ai_0 = SimpleAI(player=0, aggression=0.35, expand_threshold=40)
    ai_1 = SimpleAI(player=1, aggression=0.30, expand_threshold=50)
    print("  AIs initialized")

    # Create visualizer
    viz = GameVisualizer(
        game,
        figsize=(12, 10),
        node_scale=150,
        show_troop_counts=True,
        title="Risky Strats Demo"
    )
    print("  Visualizer ready")
    print()
    print("Starting game...")
    print("(Close the window to stop)")
    print()

    # Game step function
    def step() -> bool:
        """Advance game by one tick. Returns False to stop."""
        # Check for winner
        alive_players = [p for p in range(game.num_players) if game.player_alive[p]]
        if len(alive_players) <= 1:
            if alive_players:
                print(f"\n*** PLAYER {alive_players[0]} WINS! ***")
            else:
                print("\n*** DRAW - All players eliminated! ***")
            return False

        if game.current_tick >= max_ticks:
            print(f"\n*** MAX TICKS REACHED ({max_ticks}) ***")
            # Determine winner by total troops
            totals = []
            for p in range(game.num_players):
                if game.player_alive[p]:
                    total = sum(
                        game.get_node_troops(n)[p]
                        for n in range(game.graph.num_nodes)
                    )
                    totals.append((p, total))
            totals.sort(key=lambda x: -x[1])
            print(f"Final scores: {totals}")
            return False

        # AI actions (not every tick to reduce computation)
        if int(game.current_tick) % ai_interval == 0:
            ai_0.take_turn(game)
            ai_1.take_turn(game)

        # Advance game
        game.update()

        # Periodic status
        if int(game.current_tick) % 100 == 0:
            p0_nodes = len(game.get_player_nodes(0))
            p1_nodes = len(game.get_player_nodes(1))
            print(f"Tick {int(game.current_tick)}: P0 has {p0_nodes} nodes, P1 has {p1_nodes} nodes")

        return True

    # Run animation
    anim = viz.animate(step, interval=30, max_frames=max_ticks)
    viz.show()


def run_headless(
    grid_size: tuple[int, int] = (8, 8),
    seed: int = 42,
    ai_interval: int = 10,
    max_ticks: int = 2000
):
    """
    Run a game without visualization (for testing).

    Args:
        grid_size: (rows, cols) for the game grid.
        seed: Random seed.
        ai_interval: How often AIs take actions.
        max_ticks: Maximum game length.
    """
    print("Running headless game...")

    rng = np.random.default_rng(seed)
    n, m = grid_size
    graph = perturbed_grid(n=n, m=m, k=2.0, r=0.15, threshold=1.5, rng=rng)

    capital_p0 = 0
    capital_p1 = (n - 1) * m + (m - 1)
    game = RiskyGraph(graph, capitals=[capital_p0, capital_p1])

    ai_0 = SimpleAI(player=0, aggression=0.35, expand_threshold=40)
    ai_1 = SimpleAI(player=1, aggression=0.30, expand_threshold=50)

    winner = None
    for tick in range(max_ticks):
        alive = [p for p in range(game.num_players) if game.player_alive[p]]
        if len(alive) <= 1:
            winner = alive[0] if alive else None
            break

        if tick % ai_interval == 0:
            ai_0.take_turn(game)
            ai_1.take_turn(game)

        game.update()

        if tick % 200 == 0:
            p0_nodes = len(game.get_player_nodes(0))
            p1_nodes = len(game.get_player_nodes(1))
            print(f"  Tick {tick}: P0={p0_nodes} nodes, P1={p1_nodes} nodes")

    print(f"\nGame ended at tick {int(game.current_tick)}")
    if winner is not None:
        print(f"Winner: Player {winner}")
    else:
        print("No clear winner (draw or timeout)")

    return game


def save_gif(
    filename: str = "game.gif",
    grid_size: tuple[int, int] = (6, 6),
    seed: int = 42,
    ai_interval: int = 5,
    max_ticks: int = 500,
    fps: int = 20
):
    """Save a game as a GIF."""
    print(f"Rendering game to {filename}...")

    rng = np.random.default_rng(seed)
    n, m = grid_size
    graph = perturbed_grid(n=n, m=m, k=1.0, r=0.15, threshold=1.5, rng=rng)

    capital_p0 = 0
    capital_p1 = (n - 1) * m + (m - 1)
    game = RiskyGraph(graph, capitals=[capital_p0, capital_p1])

    ai_0 = SimpleAI(player=0, aggression=0.35, expand_threshold=40)
    ai_1 = SimpleAI(player=1, aggression=0.30, expand_threshold=50)

    viz = GameVisualizer(game, figsize=(10, 8), node_scale=100, title="Risky Strats")

    def step():
        alive = [p for p in range(game.num_players) if game.player_alive[p]]
        if len(alive) <= 1 or game.current_tick >= max_ticks:
            return False
        if int(game.current_tick) % ai_interval == 0:
            ai_0.take_turn(game)
            ai_1.take_turn(game)
        game.update()
        if int(game.current_tick) % 50 == 0:
            print(f"  Tick {int(game.current_tick)}/{max_ticks}")
        return True

    anim = viz.animate(step, interval=1000 // fps, max_frames=max_ticks)

    print("Saving (this may take a minute)...")
    anim.save(filename, writer='pillow', fps=fps)
    print(f"Saved to {filename}")
    viz.close()


def run_attention_demo(
    grid_size: tuple[int, int] = (8, 8),
    seed: int = 42,
    max_ticks: int = 5000
):
    """
    Run a demo with attention-based AIs.
    """
    print("Initializing attention AI game...")

    rng = np.random.default_rng(seed)
    graph = poisson_graph(width=15, height=15, intensity=2, threshold=4, max_degree=9, max_attempts=15)
    print(f"  Graph: {graph.num_nodes} nodes")

    # Select capitals at opposite corners based on actual positions
    # Find node closest to bottom-left (0, 0) and top-right (width, height)
    positions = graph.positions
    dist_to_origin = np.linalg.norm(positions, axis=1)
    dist_to_far_corner = np.linalg.norm(positions - np.array([8, 8]), axis=1)
    capital_p0 = int(np.argmin(dist_to_origin))
    capital_p1 = int(np.argmin(dist_to_far_corner))

    # Ensure they're different nodes
    if capital_p0 == capital_p1:
        capital_p1 = int(np.argmax(dist_to_origin))

    game = RiskyGraph(graph, capitals=[capital_p0, capital_p1])

    # Create attention AIs
    ai_0 = AttentionAI(player=0, game=game)
    ai_1 = AttentionAI(player=1, game=game)
    print("  Attention AIs initialized")

    # Visualize with P1's attention landscape
    viz = GameVisualizer(
        game,
        figsize=(12, 10),
        node_scale=150,
        show_troop_counts=True,
        title="Risky Strats - Attention AI",
        attention_ai=ai_1  # Show Player 1's attention field
    )
    print("  Visualizer ready")
    print()
    print("Starting game...")
    print("(Close the window to stop)")
    print()

    def step() -> bool:
        alive_players = [p for p in range(game.num_players) if game.player_alive[p]]
        if len(alive_players) <= 1:
            if alive_players:
                print(f"\n*** PLAYER {alive_players[0]} WINS! ***")
            return False

        if game.current_tick >= max_ticks:
            print(f"\n*** MAX TICKS ({max_ticks}) ***")
            return False

        # Attention AIs take turn every tick
        ai_0.take_turn(game)
        ai_1.take_turn(game)

        game.update()

        if int(game.current_tick) % 100 == 0:
            p0_nodes = len(game.get_player_nodes(0))
            p1_nodes = len(game.get_player_nodes(1))
            print(f"Tick {int(game.current_tick)}: P0 has {p0_nodes} nodes, P1 has {p1_nodes} nodes")

        return True

    anim = viz.animate(step, interval=30, max_frames=max_ticks)
    viz.show()


def run_vispy_demo(
    grid_size: tuple[int, int] = (8, 8),
    seed: int = 42,
    max_ticks: int = 5000
):
    """
    Run a demo with GPU-accelerated Vispy rendering.
    """
    from vispy_visualizer import VispyVisualizer

    print("Initializing Vispy GPU demo...")

    rng = np.random.default_rng(seed)
    graph = poisson_graph(width=15, height=15, intensity=2, threshold=4, max_degree=9, max_attempts=15)
    print(f"  Graph: {graph.num_nodes} nodes")

    # Select capitals at opposite corners based on actual positions
    positions = graph.positions
    dist_to_origin = np.linalg.norm(positions, axis=1)
    dist_to_far_corner = np.linalg.norm(positions - np.array([8, 8]), axis=1)
    capital_p0 = int(np.argmin(dist_to_origin))
    capital_p1 = int(np.argmin(dist_to_far_corner))

    if capital_p0 == capital_p1:
        capital_p1 = int(np.argmax(dist_to_origin))

    game = RiskyGraph(graph, capitals=[capital_p0, capital_p1])

    # Create attention AIs
    ai_0 = AttentionAI(player=0, game=game)
    ai_1 = AttentionAI(player=1, game=game)
    print("  Attention AIs initialized")

    # Create Vispy visualizer
    viz = VispyVisualizer(
        game,
        size=(1200, 900),
        title="Risky Strats - GPU Accelerated",
        attention_ai=ai_1,
        node_size=12.0,
    )
    print("  Vispy visualizer ready")
    print()
    print("Starting game...")
    print("(Close the window to stop)")
    print()

    def step() -> bool:
        alive_players = [p for p in range(game.num_players) if game.player_alive[p]]
        if len(alive_players) <= 1:
            if alive_players:
                print(f"\n*** PLAYER {alive_players[0]} WINS! ***")
            return False

        if game.current_tick >= max_ticks:
            print(f"\n*** MAX TICKS ({max_ticks}) ***")
            return False

        ai_0.take_turn(game)
        ai_1.take_turn(game)

        game.update()

        if int(game.current_tick) % 100 == 0:
            p0_nodes = len(game.get_player_nodes(0))
            p1_nodes = len(game.get_player_nodes(1))
            print(f"Tick {int(game.current_tick)}: P0 has {p0_nodes} nodes, P1 has {p1_nodes} nodes")

        return True

    # Run with timer-based animation
    viz.animate(step, interval=0.001)  # As fast as possible
    viz.show()


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "--headless":
        run_headless()
    elif len(sys.argv) > 1 and sys.argv[1] == "--gif":
        filename = sys.argv[2] if len(sys.argv) > 2 else "game.gif"
        save_gif(filename)
    elif len(sys.argv) > 1 and sys.argv[1] == "--attention":
        run_attention_demo()
    elif len(sys.argv) > 1 and sys.argv[1] == "--vispy":
        run_vispy_demo()
    else:
        run_demo()
