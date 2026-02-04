"""
Visualization for Risky Strats using matplotlib.

Renders the game state as an animated plot showing:
- Nodes colored by owner
- Troop counts
- Edges between nodes
- Traveling troops as moving dots
- Optional: attention landscape visualization
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.patheffects as path_effects
from matplotlib.animation import FuncAnimation
from matplotlib.collections import LineCollection
from matplotlib.colors import LinearSegmentedColormap
from typing import Optional, Callable, TYPE_CHECKING

from risky_graph import RiskyGraph, NodeState, NEUTRAL_GARRISON, troop_radius

if TYPE_CHECKING:
    from attention_ai import AttentionAI


# Player colors (supports up to 8 players)
PLAYER_COLORS = [
    '#e41a1c',  # Red
    '#377eb8',  # Blue
    '#4daf4a',  # Green
    '#984ea3',  # Purple
    '#ff7f00',  # Orange
    '#ffff33',  # Yellow
    '#a65628',  # Brown
    '#f781bf',  # Pink
]
UNOWNED_COLOR = '#cccccc'
CONTESTED_COLOR = '#888888'

# Attention visualization colormap (cyan = low, orange = high)
ATTENTION_CMAP = LinearSegmentedColormap.from_list(
    'attention', ['#00CED1', '#FFFFFF', '#FF8C00']  # cyan -> white -> orange
)

# Node state markers
STATE_MARKERS = {
    NodeState.DEFAULT: 'o',
    NodeState.FACTORY: 's',      # Square
    NodeState.POWERPLANT: '^',   # Triangle up
    NodeState.FORT: 'D',         # Diamond
    NodeState.ARTILLERY: 'p',    # Pentagon
}

# Conversion factor: graph units to scatter size (points²)
# scatter_size = (radius_in_graph_units * GRAPH_TO_SCATTER_SCALE)²
GRAPH_TO_SCATTER_SCALE = 18.0


def troop_scatter_size(num_troops: float) -> float:
    """Compute scatter marker size from troop_radius (the single source of truth)."""
    return (troop_radius(num_troops) * GRAPH_TO_SCATTER_SCALE) ** 2


class GameVisualizer:
    """
    Matplotlib-based game visualizer.

    Can run as animation or render single frames.
    """

    def __init__(
        self,
        game: RiskyGraph,
        figsize: tuple[float, float] = (12, 10),
        node_scale: float = 50,
        show_troop_counts: bool = True,
        show_edges: bool = True,
        title: str = "Risky Strats",
        attention_ai: Optional["AttentionAI"] = None
    ):
        """
        Args:
            game: The RiskyGraph to visualize.
            figsize: Figure size in inches.
            node_scale: Base size for nodes (scaled by troop count).
            show_troop_counts: Whether to show troop count labels.
            show_edges: Whether to draw edges between nodes.
            title: Plot title.
            attention_ai: Optional AttentionAI to visualize attention landscape.
        """
        self.game = game
        self.node_scale = node_scale
        self.show_troop_counts = show_troop_counts
        self.show_edges = show_edges
        self.title = title
        self.attention_ai = attention_ai

        # Set up figure - two panels if showing attention
        if attention_ai is not None:
            self.fig, (self.ax, self.ax_attention) = plt.subplots(
                1, 2, figsize=(figsize[0] * 1.6, figsize[1])
            )
            self.ax_attention.set_aspect('equal')
            self.ax_attention.set_title(f"P{attention_ai.player} Attention")
        else:
            self.fig, self.ax = plt.subplots(figsize=figsize)
            self.ax_attention = None

        self.ax.set_aspect('equal')
        self.ax.set_title(title)

        # Compute plot bounds from node positions
        positions = game.graph.positions
        margin = 1.0
        xlim = (positions[:, 0].min() - margin, positions[:, 0].max() + margin)
        ylim = (positions[:, 1].min() - margin, positions[:, 1].max() + margin)
        self.ax.set_xlim(*xlim)
        self.ax.set_ylim(*ylim)

        if self.ax_attention is not None:
            self.ax_attention.set_xlim(*xlim)
            self.ax_attention.set_ylim(*ylim)

        # Initialize plot elements
        self._init_edges()
        self._init_nodes()
        self._init_traveling()
        self._init_legend()
        self._init_info_text()

        if self.ax_attention is not None:
            self._init_attention_panel()

        # Draw initial state
        self.update()

    def _init_edges(self) -> None:
        """Initialize edge line collection."""
        if not self.show_edges:
            self.edge_collection = None
            return

        edges = []
        positions = self.game.graph.positions
        for i in range(self.game.graph.num_nodes):
            for j in self.game.graph.neighbors(i):
                if i < j:  # Avoid duplicates
                    edges.append([positions[i], positions[j]])

        self.edge_collection = LineCollection(
            edges, colors='#dddddd', linewidths=0.5, zorder=1
        )
        self.ax.add_collection(self.edge_collection)

    def _init_nodes(self) -> None:
        """Initialize node scatter plots (one per state type)."""
        self.node_scatters = {}
        for state in NodeState:
            scatter = self.ax.scatter(
                [], [], marker=STATE_MARKERS[state],
                s=[], c=[], edgecolors='black', linewidths=1.0, zorder=3,
                alpha=1.0
            )
            self.node_scatters[state] = scatter

        # Troop count labels - multiple per node for multi-colored display
        # Structure: troop_labels[node] = [label_line_0, label_line_1, ...]
        self.troop_labels = []
        if self.show_troop_counts:
            max_labels_per_node = self.game.num_players + 2  # players + state + garrison
            for _ in range(self.game.graph.num_nodes):
                node_labels = []
                for _ in range(max_labels_per_node):
                    label = self.ax.text(
                        0, 0, '', ha='center', va='top',
                        fontsize=8, fontweight='bold', zorder=4,
                        color='black'
                    )
                    node_labels.append(label)
                self.troop_labels.append(node_labels)

    def _init_traveling(self) -> None:
        """Initialize traveling troops scatter (below nodes)."""
        self.traveling_scatter = self.ax.scatter(
            [], [], s=[], c=[], marker='o',
            edgecolors='black', linewidths=0.5, zorder=2, alpha=1.0
        )

    def _init_legend(self) -> None:
        """Initialize player color legend."""
        patches = []
        for i in range(self.game.num_players):
            color = PLAYER_COLORS[i % len(PLAYER_COLORS)]
            patches.append(mpatches.Patch(color=color, label=f'Player {i}'))
        patches.append(mpatches.Patch(color=UNOWNED_COLOR, label='Unowned'))
        self.ax.legend(handles=patches, loc='upper left', fontsize=8)

    def _init_info_text(self) -> None:
        """Initialize info text display."""
        self.info_text = self.ax.text(
            0.02, 0.02, '', transform=self.ax.transAxes,
            fontsize=9, verticalalignment='bottom',
            family='monospace',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8)
        )

    def _init_attention_panel(self) -> None:
        """Initialize attention visualization panel with flow arrows."""
        positions = self.game.graph.positions
        n = self.game.graph.num_nodes

        # Store edge info for flow arrows
        self.attention_edges = []
        for i in range(n):
            for j in self.game.graph.neighbors(i):
                if i < j:
                    self.attention_edges.append((i, j))

        # Nodes for attention panel (initialize with neutral 0.5)
        initial_colors = np.full(n, 0.5)
        self.attention_scatter = self.ax_attention.scatter(
            positions[:, 0], positions[:, 1],
            s=self.node_scale, c=initial_colors, cmap=ATTENTION_CMAP,
            edgecolors='black', linewidths=0.5, zorder=2,
            vmin=0, vmax=1
        )

        # Initialize flow arrows (will be updated each frame)
        self.flow_arrows = []

        # Colorbar
        self.attention_cbar = self.fig.colorbar(
            self.attention_scatter, ax=self.ax_attention,
            label='Attention (normalized)', shrink=0.8
        )

    def _get_node_color(self, node: int) -> str:
        """Get color for a node based on ownership."""
        owner = self.game.get_node_owner(node)
        if owner is not None:
            return PLAYER_COLORS[owner % len(PLAYER_COLORS)]

        # Check if contested
        troops = self.game.get_node_troops(node)
        if np.sum(troops) > 0:
            return CONTESTED_COLOR
        return UNOWNED_COLOR

    def _get_node_size(self, node: int) -> float:
        """Get node size (static)."""
        return self.node_scale

    def update(self, frame: Optional[int] = None) -> list:
        """
        Update visualization for current game state.

        Args:
            frame: Frame number (unused, for FuncAnimation compatibility).

        Returns:
            List of updated artists.
        """
        artists = []
        positions = self.game.graph.positions

        # Group nodes by state
        state_nodes = {state: [] for state in NodeState}
        state_positions = {state: [] for state in NodeState}
        state_colors = {state: [] for state in NodeState}
        state_sizes = {state: [] for state in NodeState}

        for node in range(self.game.graph.num_nodes):
            state = self.game.get_node_state(node)
            state_nodes[state].append(node)
            state_positions[state].append(positions[node])
            state_colors[state].append(self._get_node_color(node))
            state_sizes[state].append(self._get_node_size(node))

        # Update scatter plots
        for state, scatter in self.node_scatters.items():
            if state_positions[state]:
                pos_array = np.array(state_positions[state])
                scatter.set_offsets(pos_array)
                scatter.set_sizes(state_sizes[state])
                # Need to disable array mapping and set colors directly
                scatter.set_array(None)
                scatter.set_facecolors(state_colors[state])
            else:
                scatter.set_offsets(np.zeros((0, 2)))
            artists.append(scatter)

        # Update troop labels (offset below node)
        label_offset = 0.35  # Offset below node center
        line_spacing = 0.22  # Spacing between label lines
        # Text outline for readability
        outline = [path_effects.withStroke(linewidth=2, foreground='black')]

        if self.show_troop_counts:
            for node, node_labels in enumerate(self.troop_labels):
                troops = self.game.get_node_troops(node)
                garrison = int(self.game.get_garrison(node))
                state = self.game.get_node_state(node)
                pos = positions[node]

                # Clear all labels for this node first
                for lbl in node_labels:
                    lbl.set_text('')

                line_idx = 0

                # State prefix on first line (if any)
                state_prefix = {
                    NodeState.FACTORY: '[F]',
                    NodeState.POWERPLANT: '[P]',
                    NodeState.FORT: '[T]',
                    NodeState.ARTILLERY: '[A]',
                }.get(state, '')
                if state_prefix:
                    node_labels[line_idx].set_position((pos[0], pos[1] - label_offset - line_idx * line_spacing))
                    node_labels[line_idx].set_text(state_prefix)
                    node_labels[line_idx].set_color('white')
                    node_labels[line_idx].set_path_effects(outline)
                    artists.append(node_labels[line_idx])
                    line_idx += 1

                # Show each player's troops with their color
                for player in range(self.game.num_players):
                    player_troops = int(troops[player])
                    if player_troops > 0:
                        node_labels[line_idx].set_position((pos[0], pos[1] - label_offset - line_idx * line_spacing))
                        node_labels[line_idx].set_text(str(player_troops))
                        node_labels[line_idx].set_color(PLAYER_COLORS[player % len(PLAYER_COLORS)])
                        node_labels[line_idx].set_path_effects(outline)
                        artists.append(node_labels[line_idx])
                        line_idx += 1

                # Garrison count (if any)
                if garrison > 0:
                    node_labels[line_idx].set_position((pos[0], pos[1] - label_offset - line_idx * line_spacing))
                    node_labels[line_idx].set_text(f'(+{garrison})')
                    node_labels[line_idx].set_color('#aaaaaa')
                    node_labels[line_idx].set_path_effects(outline)
                    artists.append(node_labels[line_idx])
                    line_idx += 1

        # Update traveling troops
        traveling = self.game.get_traveling_troops()
        if traveling:
            travel_positions = []
            travel_colors = []
            travel_sizes = []
            for troop in traveling:
                pos = self.game.get_troop_position(troop)
                travel_positions.append(pos)
                travel_colors.append(PLAYER_COLORS[troop.owner % len(PLAYER_COLORS)])
                # Size matches aggregation radius so visual touch = merge
                travel_sizes.append(troop_scatter_size(troop.num_troops))

            self.traveling_scatter.set_offsets(np.array(travel_positions))
            self.traveling_scatter.set_array(None)  # Disable colormap
            self.traveling_scatter.set_facecolors(travel_colors)
            self.traveling_scatter.set_sizes(travel_sizes)
        else:
            self.traveling_scatter.set_offsets(np.zeros((0, 2)))
        artists.append(self.traveling_scatter)

        # Update info text
        info_lines = [f"Tick: {int(self.game.current_tick)}"]
        for p in range(self.game.num_players):
            status = "ALIVE" if self.game.player_alive[p] else "DEAD"
            nodes = len(self.game.get_player_nodes(p))
            traveling_count = len(self.game.get_traveling_troops(p))
            total_troops = sum(
                self.game.get_node_troops(n)[p]
                for n in range(self.game.graph.num_nodes)
            )
            info_lines.append(
                f"P{p}: {status} | Nodes: {nodes} | "
                f"Troops: {int(total_troops)} | Moving: {traveling_count}"
            )
        self.info_text.set_text('\n'.join(info_lines))
        artists.append(self.info_text)

        # Update title
        self.ax.set_title(f"{self.title} - Tick {int(self.game.current_tick)}")

        # Update attention panel if present
        if self.ax_attention is not None and self.attention_ai is not None:
            self._update_attention_panel()

        return artists

    def _update_attention_panel(self) -> None:
        """Update the attention visualization panel with flow arrows."""
        attention = self.attention_ai.attention
        positions = self.game.graph.positions

        # Normalize attention to [0, 1] for node colors
        scale = max(np.abs(attention).max(), 1e-6)
        normalized = (attention / scale + 1) / 2
        normalized = np.clip(normalized, 0, 1)

        # Update node colors
        self.attention_scatter.set_array(normalized)

        # Remove old arrows
        for arrow in self.flow_arrows:
            arrow.remove()
        self.flow_arrows = []

        # Compute gradients and draw flow arrows on each edge
        max_gradient = 1e-6
        gradients = []
        for i, j in self.attention_edges:
            gradient = attention[j] - attention[i]  # Positive = flow from i to j
            gradients.append(gradient)
            max_gradient = max(max_gradient, abs(gradient))

        # Draw arrows for each edge
        for (i, j), gradient in zip(self.attention_edges, gradients):
            if abs(gradient) < max_gradient * 0.05:
                # Skip very weak gradients
                continue

            pos_i = positions[i]
            pos_j = positions[j]

            # Arrow goes from low attention to high attention
            if gradient > 0:
                start, end = pos_i, pos_j
            else:
                start, end = pos_j, pos_i
                gradient = -gradient

            # Arrow positioned at edge midpoint, pointing in flow direction
            mid = (start + end) / 2
            direction = end - start
            length = np.linalg.norm(direction)
            if length < 1e-6:
                continue
            direction = direction / length

            # Scale arrow by gradient magnitude
            arrow_scale = 0.3 * (gradient / max_gradient)
            arrow_width = 0.08 * (gradient / max_gradient)

            # Color based on gradient strength (orange for strong flow)
            strength = gradient / max_gradient
            color = ATTENTION_CMAP(0.5 + 0.5 * strength)  # Map to orange end

            arrow = self.ax_attention.annotate(
                '', xy=mid + direction * arrow_scale * 0.5,
                xytext=mid - direction * arrow_scale * 0.5,
                arrowprops=dict(
                    arrowstyle='->', color=color,
                    lw=1 + 2 * strength, mutation_scale=8 + 8 * strength
                ),
                zorder=1
            )
            self.flow_arrows.append(arrow)

    def render_frame(self) -> None:
        """Render a single frame (for non-animated use)."""
        self.update()
        self.fig.canvas.draw()
        plt.pause(0.001)

    def animate(
        self,
        step_callback: Callable[[], bool],
        interval: int = 50,
        max_frames: int = 10000
    ) -> FuncAnimation:
        """
        Run animated visualization.

        Args:
            step_callback: Function to call each frame to advance game state.
                           Should return False to stop animation.
            interval: Milliseconds between frames.
            max_frames: Maximum number of frames.

        Returns:
            The FuncAnimation object.
        """
        self.step_callback = step_callback
        self.animation_running = True

        def animate_frame(frame):
            if self.animation_running:
                should_continue = self.step_callback()
                if not should_continue:
                    self.animation_running = False
            return self.update()

        self.anim = FuncAnimation(
            self.fig, animate_frame,
            frames=max_frames,
            interval=interval,
            blit=False,
            repeat=False
        )
        return self.anim

    def show(self) -> None:
        """Display the plot."""
        plt.show()

    def close(self) -> None:
        """Close the plot."""
        plt.close(self.fig)
