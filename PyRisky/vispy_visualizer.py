"""
GPU-accelerated visualization for Risky Strats using Vispy.

Much faster than matplotlib - targets 500+ FPS.
"""

import time
import numpy as np
from typing import Optional, Callable, TYPE_CHECKING

# Select backend BEFORE importing scene/gloo/visuals
# GLFW handles macOS Core Profile contexts properly
import vispy
try:
    vispy.use(app='glfw')
except RuntimeError:
    pass  # Fall back to default

from vispy import app, scene, gloo
from vispy.scene import visuals

from risky_graph import RiskyGraph, NodeState, troop_radius

if TYPE_CHECKING:
    from attention_ai import AttentionAI


# Player colors (RGBA, 0-1 range)
PLAYER_COLORS = [
    (0.89, 0.10, 0.11, 1.0),  # Red
    (0.22, 0.49, 0.72, 1.0),  # Blue
    (0.30, 0.69, 0.29, 1.0),  # Green
    (0.60, 0.31, 0.64, 1.0),  # Purple
    (1.00, 0.50, 0.00, 1.0),  # Orange
    (1.00, 1.00, 0.20, 1.0),  # Yellow
    (0.65, 0.34, 0.16, 1.0),  # Brown
    (0.97, 0.51, 0.75, 1.0),  # Pink
]
UNOWNED_COLOR = (0.7, 0.7, 0.7, 1.0)
CONTESTED_COLOR = (0.5, 0.5, 0.5, 1.0)
GARRISON_COLOR = (0.4, 0.4, 0.4, 1.0)
EDGE_COLOR = (0.8, 0.8, 0.8, 0.5)


class VispyVisualizer:
    """
    GPU-accelerated game visualizer using Vispy.
    """

    def __init__(
        self,
        game: RiskyGraph,
        size: tuple[int, int] = (1200, 900),
        title: str = "Risky Strats",
        attention_ai: Optional["AttentionAI"] = None,
        node_size: float = 15.0,
    ):
        self.game = game
        self.attention_ai = attention_ai
        self.node_size = node_size

        # Create canvas
        self.canvas = scene.SceneCanvas(
            keys='interactive',
            size=size,
            title=title,
            show=False
        )

        # Set up view with 2D camera
        self.view = self.canvas.central_widget.add_view()
        self.view.camera = scene.PanZoomCamera(aspect=1)

        # Compute bounds from node positions
        positions = game.graph.positions
        margin = 1.0
        x_min, x_max = positions[:, 0].min() - margin, positions[:, 0].max() + margin
        y_min, y_max = positions[:, 1].min() - margin, positions[:, 1].max() + margin
        self.view.camera.set_range(x=(x_min, x_max), y=(y_min, y_max))

        # Initialize visual elements
        self._init_edges()
        self._init_nodes()
        self._init_traveling()
        self._init_labels()
        self._init_info()

        if attention_ai is not None:
            self._init_attention()

        # Initial update
        self.update()

    def _init_edges(self) -> None:
        """Initialize edge lines (static)."""
        positions = self.game.graph.positions
        n = self.game.graph.num_nodes

        # Build edge coordinate array (need 3D for vispy)
        edges = []
        for i in range(n):
            for j in self.game.graph.neighbors(i):
                if i < j:
                    # Add z=0 coordinate
                    edges.append([positions[i, 0], positions[i, 1], 0])
                    edges.append([positions[j, 0], positions[j, 1], 0])

        if edges:
            edge_pos = np.array(edges, dtype=np.float32)
            self.edges = visuals.Line(
                pos=edge_pos,
                color=EDGE_COLOR,
                connect='segments',
                width=1.0,
                parent=self.view.scene
            )
        else:
            self.edges = None

    def _init_nodes(self) -> None:
        """Initialize node markers."""
        positions = self.game.graph.positions
        n = self.game.graph.num_nodes

        # 3D positions (vispy wants z coordinate)
        pos_3d = np.zeros((n, 3), dtype=np.float32)
        pos_3d[:, :2] = positions

        self.node_markers = visuals.Markers(parent=self.view.scene)
        self.node_markers.set_data(
            pos=pos_3d,
            face_color=np.full((n, 4), UNOWNED_COLOR, dtype=np.float32),
            size=self.node_size,
            edge_color='black',
            edge_width=1.0,
        )
        self._node_pos_3d = pos_3d

    def _init_traveling(self) -> None:
        """Initialize traveling troops markers."""
        self.troop_markers = visuals.Markers(parent=self.view.scene)
        # Start with a dummy point (vispy doesn't like empty arrays)
        self.troop_markers.set_data(
            pos=np.array([[0, 0, 0]], dtype=np.float32),
            face_color=np.array([[0, 0, 0, 0]], dtype=np.float32),  # Invisible
            size=1,
        )
        self._troop_visible = False

    def _init_labels(self) -> None:
        """Initialize text labels for nodes."""
        positions = self.game.graph.positions
        n = self.game.graph.num_nodes

        # Create text visual for labels
        # We'll update the text each frame
        self.label_texts = []
        for i in range(n):
            text = visuals.Text(
                text='',
                pos=(positions[i, 0], positions[i, 1] - 0.4, 0),
                color='white',
                font_size=8,
                anchor_x='center',
                anchor_y='top',
                parent=self.view.scene
            )
            self.label_texts.append(text)

    def _init_info(self) -> None:
        """Initialize info text display."""
        # Use a separate text visual anchored to screen coordinates
        self.info_text = visuals.Text(
            text='',
            pos=(10, 10),
            color='white',
            font_size=10,
            anchor_x='left',
            anchor_y='bottom',
            parent=self.canvas.scene  # Attach to canvas, not view
        )

    def _init_attention(self) -> None:
        """Initialize attention flow arrows."""
        self.attention_arrows = []
        # We'll create/update arrows dynamically

    def _get_node_color(self, node: int) -> tuple:
        """Get color for a node based on ownership."""
        owner = self.game.get_node_owner(node)
        if owner is not None:
            return PLAYER_COLORS[owner % len(PLAYER_COLORS)]

        troops = self.game.get_node_troops(node)
        if np.sum(troops) > 0:
            return CONTESTED_COLOR
        return UNOWNED_COLOR

    def update(self) -> None:
        """Update visualization for current game state."""
        self._update_nodes()
        self._update_traveling()
        self._update_labels()
        self._update_info()

        if self.attention_ai is not None:
            self._update_attention()

    def _update_nodes(self) -> None:
        """Update node colors based on ownership."""
        n = self.game.graph.num_nodes
        colors = np.array([self._get_node_color(i) for i in range(n)], dtype=np.float32)
        self.node_markers.set_data(
            pos=self._node_pos_3d,
            face_color=colors,
            size=self.node_size,
            edge_color='black',
            edge_width=1.0,
        )

    def _update_traveling(self) -> None:
        """Update traveling troops positions and colors."""
        traveling = self.game.get_traveling_troops()

        if not traveling:
            # Hide with invisible dummy point
            if self._troop_visible:
                self.troop_markers.set_data(
                    pos=np.array([[0, 0, 0]], dtype=np.float32),
                    face_color=np.array([[0, 0, 0, 0]], dtype=np.float32),
                    size=1,
                )
                self._troop_visible = False
            return

        n = len(traveling)
        positions = np.zeros((n, 3), dtype=np.float32)
        colors = np.zeros((n, 4), dtype=np.float32)
        sizes = np.zeros(n, dtype=np.float32)

        for i, troop in enumerate(traveling):
            pos = self.game.get_troop_position(troop)
            positions[i, 0] = pos[0]
            positions[i, 1] = pos[1]
            colors[i] = PLAYER_COLORS[troop.owner % len(PLAYER_COLORS)]
            # Size based on troop_radius, scaled for visibility
            sizes[i] = troop_radius(troop.num_troops) * 50

        self.troop_markers.set_data(
            pos=positions,
            face_color=colors,
            size=sizes,
            edge_color='black',
            edge_width=0.5,
        )
        self._troop_visible = True

    def _update_labels(self) -> None:
        """Update node labels."""
        positions = self.game.graph.positions

        for node, text_visual in enumerate(self.label_texts):
            troops = self.game.get_node_troops(node)
            garrison = int(self.game.get_garrison(node))

            # Build label text
            parts = []
            for player in range(self.game.num_players):
                player_troops = int(troops[player])
                if player_troops > 0:
                    parts.append(str(player_troops))

            if garrison > 0:
                parts.append(f'+{garrison}')

            label = ' '.join(parts)
            text_visual.text = label

            # Color based on majority owner
            owner = self.game.get_node_owner(node)
            if owner is not None:
                text_visual.color = PLAYER_COLORS[owner % len(PLAYER_COLORS)]
            elif garrison > 0:
                text_visual.color = GARRISON_COLOR
            else:
                text_visual.color = 'white'

    def _update_info(self) -> None:
        """Update info text."""
        # FPS calculation
        fps_str = ""
        if hasattr(self, '_fps_frames'):
            self._fps_frames += 1
            elapsed = time.perf_counter() - self._fps_start
            if elapsed >= 1.0:
                fps = self._fps_frames / elapsed
                fps_str = f" | FPS: {fps:.0f}"
                self._fps_frames = 0
                self._fps_start = time.perf_counter()
        else:
            self._fps_frames = 0
            self._fps_start = time.perf_counter()

        lines = [f"Tick: {int(self.game.current_tick)}{fps_str}"]
        for p in range(self.game.num_players):
            status = "ALIVE" if self.game.player_alive[p] else "DEAD"
            nodes = len(self.game.get_player_nodes(p))
            traveling = len(self.game.get_traveling_troops(p))
            total = sum(
                self.game.get_node_troops(n)[p]
                for n in range(self.game.graph.num_nodes)
            )
            lines.append(f"P{p}: {status} | Nodes: {nodes} | Troops: {int(total)} | Moving: {traveling}")

        self.info_text.text = '\n'.join(lines)

    def _update_attention(self) -> None:
        """Update attention visualization."""
        # Simplified: just update node colors based on attention
        # Full arrow implementation would need more work
        pass

    def animate(
        self,
        step_callback: Callable[[], bool],
        interval: float = 0.016,  # ~60 FPS default
    ) -> None:
        """
        Run animated visualization.

        Args:
            step_callback: Called each frame to advance game state.
                          Return False to stop.
            interval: Seconds between frames (0.016 = 60 FPS).
        """
        self._step_callback = step_callback
        self._running = True

        def on_timer(event):
            if self._running:
                should_continue = self._step_callback()
                if not should_continue:
                    self._running = False
                self.update()
                self.canvas.update()

        self._timer = app.Timer(interval=interval, connect=on_timer, start=True)

    def show(self) -> None:
        """Show the canvas and start event loop."""
        self.canvas.show()
        app.run()

    def close(self) -> None:
        """Close the visualization."""
        self.canvas.close()
