"""A faithful miniature of the game's map generator, for concept diagrams.

The real generator (src/engine/graph.cpp) samples Poisson points in a region,
connects every pair within a distance threshold, and culls until all degrees
are <= max_neighbors. The result is a mesh-like geometric graph: triangles
everywhere, small dense pockets, no long clean cycles and no high-degree
stars. Diagrams drawn on anything else misrepresent the game, so this module
reproduces the construction (with a minimum spacing for legibility, and the
degree cap enforced by dropping longest edges rather than deleting nodes).
"""
from __future__ import annotations

from typing import List, Tuple

import numpy as np


def poisson_patch(seed: int, *, n: int = 26, width: float = 5.2,
                  height: float = 3.2, radius: float = 1.25,
                  min_dist: float = 0.62, max_deg: int = 6
                  ) -> Tuple[np.ndarray, List[Tuple[int, int]]]:
    """Return (points[n,2], edges) for a game-like geometric graph patch."""
    rng = np.random.default_rng(seed)
    pts: List[np.ndarray] = []
    tries = 0
    while len(pts) < n and tries < 4000:
        tries += 1
        p = rng.uniform((0, 0), (width, height))
        if all(np.linalg.norm(p - q) >= min_dist for q in pts):
            pts.append(p)
    P = np.array(pts)

    edges = [(i, j) for i in range(len(P)) for j in range(i + 1, len(P))
             if np.linalg.norm(P[i] - P[j]) <= radius]

    # Degree cap: repeatedly drop the longest edge at any over-degree node.
    def degrees():
        d = np.zeros(len(P), dtype=int)
        for i, j in edges:
            d[i] += 1
            d[j] += 1
        return d

    d = degrees()
    while d.max() > max_deg:
        v = int(d.argmax())
        incident = [(np.linalg.norm(P[a] - P[b]), (a, b))
                    for (a, b) in edges if v in (a, b)]
        edges.remove(max(incident)[1])
        d = degrees()
    return P, edges


def neighbors(n: int, edges) -> List[List[int]]:
    adj: List[List[int]] = [[] for _ in range(n)]
    for i, j in edges:
        adj[i].append(j)
        adj[j].append(i)
    return adj


def anneal_two_coloring(P: np.ndarray, edges, *, seed: int = 0,
                        iters: int = 4000) -> np.ndarray:
    """Max-cut style two-coloring (+1/-1) by simulated annealing --- the same
    objective the economy solvers optimize (unlike neighbors preferred)."""
    rng = np.random.default_rng(seed)
    s = rng.choice([-1, 1], size=len(P))
    adj = neighbors(len(P), edges)

    def gain(i):  # cut improvement from flipping i
        return sum(1 if s[i] == s[j] else -1 for j in adj[i])

    for it in range(iters):
        t = max(0.01, 1.5 * (1 - it / iters))
        i = rng.integers(len(P))
        g = gain(i)
        if g > 0 or rng.random() < np.exp(g / t):
            s[i] = -s[i]
    return s


def shortest_path(P: np.ndarray, edges, src: int, dst: int) -> List[int]:
    """Dijkstra by Euclidean edge length; returns the node path src..dst."""
    import heapq
    adj = {}
    for i, j in edges:
        wij = float(np.linalg.norm(P[i] - P[j]))
        adj.setdefault(i, []).append((j, wij))
        adj.setdefault(j, []).append((i, wij))
    dist = {src: 0.0}
    prev = {}
    pq = [(0.0, src)]
    while pq:
        d, u = heapq.heappop(pq)
        if u == dst:
            break
        if d > dist.get(u, np.inf):
            continue
        for v, w in adj.get(u, []):
            nd = d + w
            if nd < dist.get(v, np.inf):
                dist[v], prev[v] = nd, u
                heapq.heappush(pq, (nd, v))
    path = [dst]
    while path[-1] != src:
        path.append(prev[path[-1]])
    return path[::-1]


def graph_poisson_potential(P: np.ndarray, edges,
                            b: np.ndarray) -> np.ndarray:
    """Solve the graph Poisson equation L phi = b (least squares, zero-mean)
    --- literally what the gradient transport solver does each tick."""
    n = len(P)
    L = np.zeros((n, n))
    for i, j in edges:
        L[i, i] += 1
        L[j, j] += 1
        L[i, j] -= 1
        L[j, i] -= 1
    phi = np.linalg.lstsq(L, b - b.mean(), rcond=None)[0]
    return phi - phi.min()
