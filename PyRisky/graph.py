"""
Graph: Geometric graph implementation for Risky Strats.

A geometric graph is a graph where nodes have positions in R^2 and edges
connect nodes within a certain distance. This module provides the Graph class
and factory functions for generating perturbed grids and Poisson process graphs.
"""

from __future__ import annotations
from collections import deque
import numpy as np
from typing import Optional


class Graph:
    """
    A geometric graph with nodes positioned in R^2.

    Attributes:
        num_nodes: Number of nodes in the graph.
        positions: Array of shape (num_nodes, 2) with (x, y) coordinates.
    """

    def __init__(
        self,
        positions: np.ndarray,
        neighbors: Optional[list[set[int]]] = None
    ):
        """
        Initialize a Graph.

        Args:
            positions: Array of shape (N, 2) with node positions.
            neighbors: Optional list of sets, where neighbors[i] contains
                       indices of nodes adjacent to node i. If None, no edges.
        """
        self.positions = np.asarray(positions, dtype=float)
        self.num_nodes = len(self.positions)

        if neighbors is None:
            self._neighbors: list[set[int]] = [set() for _ in range(self.num_nodes)]
        else:
            self._neighbors = [set(n) for n in neighbors]

    def neighbors(self, node: int) -> list[int]:
        """Return list of node indices adjacent to the given node."""
        return list(self._neighbors[node])

    def are_neighbors(self, node_a: int, node_b: int) -> bool:
        """Return True if node_a and node_b are adjacent."""
        return node_b in self._neighbors[node_a]

    def distance(self, node_a: int, node_b: int) -> float:
        """Return Euclidean distance between two nodes."""
        return float(np.linalg.norm(self.positions[node_a] - self.positions[node_b]))

    def subgraph(self, nodes: list[int]) -> Graph:
        """
        Return a new Graph containing only the specified nodes.

        Edges are preserved between nodes that are both in the subset.
        Node indices in the new graph are remapped to 0..len(nodes)-1.

        Args:
            nodes: List of node indices to include.

        Returns:
            A new Graph with the subset of nodes.
        """
        nodes = list(nodes)
        old_to_new = {old: new for new, old in enumerate(nodes)}
        new_positions = self.positions[nodes]

        new_neighbors: list[set[int]] = []
        for old_idx in nodes:
            new_nbrs = set()
            for old_nbr in self._neighbors[old_idx]:
                if old_nbr in old_to_new:
                    new_nbrs.add(old_to_new[old_nbr])
            new_neighbors.append(new_nbrs)

        return Graph(new_positions, new_neighbors)

    def is_connected(self) -> bool:
        """Return True if the graph is connected (all nodes reachable from node 0)."""
        if self.num_nodes == 0:
            return True

        visited = set()
        queue = deque([0])
        visited.add(0)

        while queue:
            node = queue.popleft()
            for nbr in self._neighbors[node]:
                if nbr not in visited:
                    visited.add(nbr)
                    queue.append(nbr)

        return len(visited) == self.num_nodes


def induced_graph(positions: np.ndarray, threshold: float = 1.0) -> Graph:
    """
    Create the induced geometric graph from a set of positions.

    Nodes i and j are connected iff ||positions[i] - positions[j]|| <= threshold.

    Args:
        positions: Array of shape (N, 2) with node positions.
        threshold: Maximum distance for edge connection.

    Returns:
        A Graph with edges between all sufficiently close nodes.
    """
    positions = np.asarray(positions, dtype=float)
    n = len(positions)

    neighbors: list[set[int]] = [set() for _ in range(n)]
    for i in range(n):
        for j in range(i + 1, n):
            dist = np.linalg.norm(positions[i] - positions[j])
            if dist <= threshold:
                neighbors[i].add(j)
                neighbors[j].add(i)

    return Graph(positions, neighbors)


def perturbed_grid(
    n: int,
    m: int,
    k: float = 1.0,
    r: float = 0.0,
    threshold: float = 1.0,
    rng: Optional[np.random.Generator] = None,
    max_attempts: int = 10
) -> Graph:
    """
    Create a perturbed grid graph.

    Generates an n x m grid with spacing k, then perturbs each node by a
    random offset sampled uniformly from [-r, r] x [-r, r].

    Args:
        n: Number of rows.
        m: Number of columns.
        k: Grid spacing.
        r: Maximum perturbation magnitude per axis.
        threshold: Distance threshold for edge connectivity.
        rng: NumPy random generator. If None, uses default.
        max_attempts: Maximum attempts to generate a connected graph.

    Returns:
        A connected Graph representing the perturbed grid.

    Raises:
        RuntimeError: If no connected graph found after max_attempts.
    """
    if rng is None:
        rng = np.random.default_rng()

    for attempt in range(max_attempts):
        # Generate base grid positions
        positions = []
        for i in range(n):
            for j in range(m):
                base = np.array([i * k, j * k])
                delta = rng.uniform(-r, r, size=2)
                positions.append(base + delta)

        positions = np.array(positions)
        graph = induced_graph(positions, threshold)

        if graph.is_connected():
            return graph

    raise RuntimeError(
        f"Failed to generate connected perturbed grid after {max_attempts} attempts"
    )


def poisson_graph(
    width: float,
    height: float,
    intensity: float,
    threshold: float = 1.0,
    max_degree: int = 7,
    rng: Optional[np.random.Generator] = None,
    max_attempts: int = 10
) -> Graph:
    """
    Create a Poisson process graph.

    Samples points from a Poisson process in [0, width] x [0, height],
    constructs the induced geometric graph, then iteratively prunes overly
    clustered nodes and re-populates until a connected graph with bounded
    degree is found.

    Args:
        width: Width of the sampling region.
        height: Height of the sampling region.
        intensity: Expected number of points per unit area.
        threshold: Distance threshold for edge connectivity.
        max_degree: Maximum allowed node degree. Nodes exceeding this are pruned.
        rng: NumPy random generator. If None, uses default.
        max_attempts: Maximum attempts to generate a connected graph.

    Returns:
        A connected Graph representing the Poisson process graph.

    Raises:
        RuntimeError: If no connected graph found after max_attempts.
    """
    if rng is None:
        rng = np.random.default_rng()

    area = width * height
    target_num_points = rng.poisson(intensity * area)

    for attempt in range(max_attempts):
        # Sample initial positions
        num_points = max(target_num_points, 2)
        positions = np.column_stack([
            rng.uniform(0, width, num_points),
            rng.uniform(0, height, num_points)
        ])

        # Iteratively prune clustered nodes and re-populate
        # Use more iterations for pruning since each only removes one node
        for _ in range(max_attempts * num_points):
            graph = induced_graph(positions, threshold)

            # Find nodes exceeding max_degree
            degrees = [len(graph._neighbors[i]) for i in range(graph.num_nodes)]
            high_degree_nodes = [
                i for i in range(graph.num_nodes) if degrees[i] > max_degree
            ]

            if not high_degree_nodes:
                if graph.is_connected():
                    # All nodes within degree bound and connected - success!
                    return graph
                # Disconnected but degree-bounded: add a random point to reconnect
                new_point = np.array([[
                    rng.uniform(0, width),
                    rng.uniform(0, height)
                ]])
                positions = np.vstack([positions, new_point])
            else:
                # Find min distance to any neighbor for each node
                def min_neighbor_dist(node: int) -> float:
                    return min(graph.distance(node, nbr) for nbr in graph._neighbors[node])

                # Remove the node with the closest neighbor
                high_degree_nodes.sort(key=min_neighbor_dist)
                node_to_remove = high_degree_nodes[0]

                # Remove the node (don't add replacement - let density decrease)
                keep_mask = np.ones(len(positions), dtype=bool)
                keep_mask[node_to_remove] = False
                positions = positions[keep_mask]

    raise RuntimeError(
        f"Failed to generate connected Poisson graph after {max_attempts} attempts"
    )
