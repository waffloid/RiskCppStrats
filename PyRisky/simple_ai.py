"""
Simple greedy AI for Risky Strats.

A dumb but functional AI that:
1. Expands toward unowned/enemy nodes
2. Builds factories when affordable
3. Reinforces contested borders
"""

import numpy as np
from typing import Optional
from risky_graph import RiskyGraph, NodeState, COST_FACTORY


class SimpleAI:
    """
    Greedy heuristic AI player.

    Strategy:
    - Prioritize expanding to unowned nodes
    - Attack weakly-held enemy nodes
    - Build factories on safe interior nodes
    - Send percentage of troops to avoid depleting nodes
    """

    def __init__(self, player: int, aggression: float = 0.3, expand_threshold: float = 50):
        """
        Args:
            player: Player index this AI controls.
            aggression: Fraction of troops to send when attacking (0-1).
            expand_threshold: Minimum troops at a node before considering expansion.
        """
        self.player = player
        self.aggression = aggression
        self.expand_threshold = expand_threshold

    def take_turn(self, game: RiskyGraph) -> None:
        """
        Execute one turn of AI decision-making.

        Args:
            game: The current game state.
        """
        if not game.player_alive[self.player]:
            return

        my_nodes = game.get_player_nodes(self.player)
        if not my_nodes:
            return

        # Categorize nodes
        frontier_nodes = []  # Nodes adjacent to non-owned nodes
        interior_nodes = []  # Nodes only adjacent to owned nodes

        for node in my_nodes:
            neighbors = game.graph.neighbors(node)
            has_non_owned_neighbor = any(
                game.get_node_owner(n) != self.player for n in neighbors
            )
            if has_non_owned_neighbor:
                frontier_nodes.append(node)
            else:
                interior_nodes.append(node)

        # 1. Try to build factories on safe interior nodes
        self._try_build_factories(game, interior_nodes)

        # 2. Expand from frontier nodes
        self._expand_from_frontier(game, frontier_nodes)

        # 3. Reinforce frontier from interior
        self._reinforce_frontier(game, interior_nodes, frontier_nodes)

    def _try_build_factories(self, game: RiskyGraph, interior_nodes: list[int]) -> None:
        """Build factories on interior nodes if affordable."""
        for node in interior_nodes:
            troops = game.get_node_troops(node)[self.player]
            state = game.get_node_state(node)

            # Don't overbuild - only build if we have plenty of troops
            if state == NodeState.DEFAULT and troops > COST_FACTORY * 2:
                game.set_node_state(node, NodeState.FACTORY, self.player)

    def _expand_from_frontier(self, game: RiskyGraph, frontier_nodes: list[int]) -> None:
        """Send troops from frontier nodes toward unowned/enemy nodes."""
        for node in frontier_nodes:
            troops = game.get_node_troops(node)[self.player]
            if troops < self.expand_threshold:
                continue

            neighbors = game.graph.neighbors(node)

            # Find best target: prefer unowned, then weakest enemy
            best_target = None
            best_score = -np.inf

            for neighbor in neighbors:
                owner = game.get_node_owner(neighbor)

                if owner == self.player:
                    continue  # Skip own nodes

                neighbor_troops = game.get_node_troops(neighbor)
                total_enemy = np.sum(neighbor_troops) - neighbor_troops[self.player]

                if owner is None and total_enemy == 0:
                    # Unowned and empty - very attractive
                    score = 1000
                elif owner is None:
                    # Contested - somewhat attractive
                    score = 500 - total_enemy
                else:
                    # Enemy owned - score based on weakness
                    score = 100 - total_enemy

                if score > best_score:
                    best_score = score
                    best_target = neighbor

            if best_target is not None:
                send_amount = troops * self.aggression
                if send_amount >= 10:  # Minimum to bother sending
                    game.send_troops(node, best_target, self.player, send_amount)

    def _reinforce_frontier(
        self,
        game: RiskyGraph,
        interior_nodes: list[int],
        frontier_nodes: list[int]
    ) -> None:
        """Send troops from interior to frontier."""
        if not frontier_nodes:
            return

        for node in interior_nodes:
            troops = game.get_node_troops(node)[self.player]
            if troops < self.expand_threshold:
                continue

            # Find nearest frontier node
            best_frontier = None
            best_dist = np.inf

            node_pos = game.graph.positions[node]
            for frontier in frontier_nodes:
                frontier_pos = game.graph.positions[frontier]
                dist = np.linalg.norm(node_pos - frontier_pos)
                if dist < best_dist:
                    best_dist = dist
                    best_frontier = frontier

            if best_frontier is not None:
                send_amount = troops * 0.5  # Send half to frontier
                if send_amount >= 10:
                    game.send_troops(node, best_frontier, self.player, send_amount)
