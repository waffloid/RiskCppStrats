"""
Attention-based AI for Risky Strats.

Troops flow according to attention gradients. The AI generates attention deltas
via heuristics, attention diffuses across the graph, and troop movement emerges
from the resulting gradient field.

Key mechanics:
- Each node has an attention value (private per player)
- Troops flow from low attention to high attention
- Outflow rate = softmax(||nbr_attention⁺||) * prorate by contribution
- Attention diffuses each tick (fast diffusion ≈ solving Laplace equation)
- When gradient flips, traveling troops retreat
- Opposing troops on same edge: larger stack forces smaller to retreat
"""

import numpy as np
from typing import Optional
from collections import defaultdict

from risky_graph import RiskyGraph, NodeState, TravelingTroops, troop_radius


# Attention parameters
DIFFUSION_RATE = 0.02          # Diffusion rate (too high = numerical instability)
OUTFLOW_RATE = 0.1            # Fraction of troops to send per tick when gradient exists
MIN_TROOPS_TO_SEND = 5        # Don't send tiny amounts

# Heuristic attention deltas (applied per tick, so keep small)
ATTENTION_UNOCCUPIED_BORDER = 5.0     # Attract troops to unowned neighboring nodes
ATTENTION_ENEMY_BORDER = 1.5          # Attract troops to enemy neighboring nodes
ATTENTION_FACTORY_POWERPLANT_DELTA_DESIRE = 0.8        # Nodes wanting to become factories
ATTENTION_THREAT_RESPONSE = 2.0       # Respond to nearby enemy troops


class AttentionAI:
    """
    Attention-based AI player.

    Maintains a private attention field over the graph. Each tick:
    1. Generate attention deltas from heuristics
    2. Apply deltas to attention field
    3. Diffuse attention across graph
    4. Normalize attention (subtract mean)
    5. Compute troop flow from gradients
    6. Handle retreats when gradients flip
    7. Handle opposing troop collisions
    """

    def __init__(self, player: int, game: RiskyGraph):
        """
        Args:
            player: Player index this AI controls.
            game: The game state (used for graph structure).
        """
        self.player = player
        self.game = game
        self.num_nodes = game.graph.num_nodes

        # Private attention field
        self.attention = np.zeros(self.num_nodes, dtype=float)

        # Track last gradient sign for retreat detection
        # Key: TravelingTroops id -> sign of (dest_attention - src_attention) when sent
        self._troop_gradient_sign: dict[int, float] = {}

        # Precompute graph Laplacian for diffusion
        self._laplacian = self._build_laplacian() * 0.95

    def _build_laplacian(self) -> np.ndarray:
        """Build the graph Laplacian matrix for diffusion."""
        n = self.num_nodes
        L = np.zeros((n, n), dtype=float)

        for i in range(n):
            neighbors = self.game.graph.neighbors(i)
            degree = len(neighbors)
            L[i, i] = degree
            for j in neighbors:
                L[i, j] = -1

        return L

    def take_turn(self, game: RiskyGraph) -> None:
        """
        Execute one tick of attention-based AI.

        Args:
            game: Current game state.
        """
        self.game = game

        if not game.player_alive[self.player]:
            return

        # 1. Generate attention deltas from heuristics
        deltas = self._generate_attention_deltas()

        # 2. Diffuse attention
        self._diffuse_attention()

        # 3. Apply deltas
        self.attention += deltas

        # 4. Normalize (subtract mean to prevent drift)
        self.attention -= np.mean(self.attention)

        # 5. Handle retreats (check gradient flips)
        self._handle_retreats()

        # 6. Compute and execute troop flow
        self._execute_troop_flow()

        # 7. Try to build structures
        self._try_build_structures()

        # NOTE: Opposing troop collisions should be handled by game engine, not AI
        # (future feature)

    def _generate_attention_deltas(self) -> np.ndarray:
        """
        Generate attention deltas from heuristics.

        Returns:
            Array of attention deltas per node.
        """
        deltas = np.zeros(self.num_nodes, dtype=float)
        my_nodes = set(self.game.get_player_nodes(self.player))

        for node in range(self.num_nodes):
            owner = self.game.get_node_owner(node)
            neighbors = self.game.graph.neighbors(node)

            if self.game.is_capital(node):
                continue

            if owner == self.player:
                # === Owned node heuristics ===

                # Check for unoccupied/enemy neighbors (border expansion)
                #for nbr in neighbors:
                #    nbr_owner = self.game.get_node_owner(nbr)
                #    if nbr_owner is None:
                #        # Unoccupied neighbor - attract troops to this node
                #        deltas[node] += ATTENTION_UNOCCUPIED_BORDER
                #    elif nbr_owner != self.player:
                #        # Enemy neighbor - higher priority
                #        deltas[node] += ATTENTION_ENEMY_BORDER

                # Powerplant desire: good if surrounded by factories/capitals
                # (would boost their production)
                state = self.game.get_node_state(node)
                if state == NodeState.DEFAULT or state == NodeState.FACTORY:
                    factory_neighbors = sum(
                        1 for nbr in neighbors
                        if (self.game.get_node_owner(nbr) == self.player and
                            (self.game.get_node_state(nbr) == NodeState.FACTORY or
                             self.game.is_capital(nbr)))
                    )

                    powerplant_neighbors = sum(
                        1 for nbr in neighbors
                        if (self.game.get_node_owner(nbr) == self.player and
                            self.game.get_node_state(nbr) == NodeState.POWERPLANT)
                    )

                    deltas[node] += (factory_neighbors - powerplant_neighbors) * ATTENTION_FACTORY_POWERPLANT_DELTA_DESIRE

                    if state == NodeState.DEFAULT:
                        deltas[node] += ATTENTION_UNOCCUPIED_BORDER


            elif owner is None and node not in my_nodes:
                # === Unowned node (potential expansion target) ===
                # Generate attention if adjacent to owned node
                if any(self.game.get_node_owner(nbr) == self.player for nbr in neighbors):
                    # This is a border node we could expand to
                    # Attention goes to our adjacent nodes to draw troops
                    #for nbr in neighbors:
                    #    if self.game.get_node_owner(nbr) == self.player:
                    deltas[node] += ATTENTION_UNOCCUPIED_BORDER

            else:
                pass
                # === Enemy node ===
                # Threat response: if enemy troops near our border
                #enemy_troops = self.game.get_node_troops(node)
                #enemy_total = np.sum(enemy_troops) - enemy_troops[self.player]
                #if enemy_total > 0:
                #    for nbr in neighbors:
                #        if self.game.get_node_owner(nbr) == self.player:
                #            # Scale by threat level
                #            threat = min(enemy_total / 100.0, 1.0)
                #            deltas[nbr] += ATTENTION_THREAT_RESPONSE * threat

        return deltas

    def _diffuse_attention(self) -> None:
        """
        Diffuse attention across the graph.

        Uses discrete Laplacian: new_attention = attention - rate * L @ attention
        High rate ≈ solving Laplace equation (instant equilibrium from sources).
        """
        # Laplacian diffusion step
        self.attention -= DIFFUSION_RATE * (self._laplacian @ self.attention)

        # Clamp to prevent numerical instability
        self.attention = np.clip(self.attention, -1000, 1000)

    def _handle_retreats(self) -> None:
        """
        Check for gradient flips and trigger retreats.

        For each traveling troop, compare current gradient sign to when it was sent.
        If flipped, retreat the troop.
        """
        my_troops = self.game.get_traveling_troops(player=self.player)
        to_retreat = []

        for troop in my_troops:
            troop_id = id(troop)

            # Current gradient: destination attention - source attention
            # (source is from_node for current segment)
            current_gradient = (
                self.attention[troop.local_to_node] -
                self.attention[troop.from_node]
            )

            if troop_id in self._troop_gradient_sign:
                original_sign = self._troop_gradient_sign[troop_id]
                current_sign = np.sign(current_gradient)

                # Gradient flipped if signs differ and original was positive
                # (we only sent troops when gradient was positive)
                if original_sign > 0 and current_sign <= 0:
                    to_retreat.append(troop)
                    del self._troop_gradient_sign[troop_id]
            else:
                # First time seeing this troop, record its gradient sign
                self._troop_gradient_sign[troop_id] = np.sign(current_gradient)

        # Execute retreats
        for troop in to_retreat:
            self.game.retreat_troops(troop)

        # Clean up tracking for troops that no longer exist
        current_troop_ids = {id(t) for t in my_troops}
        self._troop_gradient_sign = {
            k: v for k, v in self._troop_gradient_sign.items()
            if k in current_troop_ids
        }

    def _handle_collisions(self) -> None:
        """
        Handle opposing troop collisions on edges.

        When troops from different players are on the same edge traveling
        in opposite directions, the larger stack forces the smaller to retreat.
        Collision occurs at aggregation distance.
        """
        # Group all traveling troops by edge (unordered node pair)
        edge_troops: dict[tuple[int, int], list[TravelingTroops]] = defaultdict(list)

        for troop in self.game.get_traveling_troops():
            edge = tuple(sorted([troop.from_node, troop.local_to_node]))
            edge_troops[edge].append(troop)

        for edge, troops in edge_troops.items():
            if len(troops) < 2:
                continue

            # Group by direction and owner
            # direction: True if going from edge[0] to edge[1]
            groups: dict[tuple[int, bool], list[TravelingTroops]] = defaultdict(list)
            for troop in troops:
                direction = troop.from_node == edge[0]
                groups[(troop.owner, direction)].append(troop)

            # Find opposing groups (different owners, opposite directions)
            owners_directions = list(groups.keys())
            for i, (owner_i, dir_i) in enumerate(owners_directions):
                for owner_j, dir_j in owners_directions[i + 1:]:
                    if owner_i != owner_j and dir_i != dir_j:
                        # Opposing groups found
                        group_i = groups[(owner_i, dir_i)]
                        group_j = groups[(owner_j, dir_j)]

                        # Check for collision (within aggregation distance)
                        # Simplification: check if any pair is close enough
                        collision = False
                        for ti in group_i:
                            pos_i = np.array(self.game.get_troop_position(ti))
                            radius_i = troop_radius(ti.num_troops)
                            for tj in group_j:
                                pos_j = np.array(self.game.get_troop_position(tj))
                                radius_j = troop_radius(tj.num_troops)
                                if np.linalg.norm(pos_i - pos_j) <= radius_i + radius_j:
                                    collision = True
                                    break
                            if collision:
                                break

                        if collision:
                            # Sum troops in each group
                            total_i = sum(t.num_troops for t in group_i)
                            total_j = sum(t.num_troops for t in group_j)

                            # Smaller group retreats
                            if total_i < total_j:
                                for t in group_i:
                                    if t.owner == self.player:
                                        self.game.retreat_troops(t)
                            elif total_j < total_i:
                                for t in group_j:
                                    if t.owner == self.player:
                                        self.game.retreat_troops(t)
                            # Equal: both retreat? Or neither? For now, neither.

    def _execute_troop_flow(self) -> None:
        """
        Compute and execute troop flow based on attention gradients.

        For each owned node:
        1. Compute nbr_attention_diff = [nbr.attention - self.attention for each nbr]
        2. Compute positive part: nbr_attention⁺ = max(0, nbr_attention_diff)
        3. Total outflow rate = softmax(||nbr_attention⁺||) ∈ [0, 1]
        4. Prorate outflow to each neighbor by nbr_attention⁺_i / ||nbr_attention⁺||
        """
        my_nodes = self.game.get_player_nodes(self.player)

        for node in my_nodes:
            troops_here = self.game.get_node_troops(node)[self.player]
            if troops_here < MIN_TROOPS_TO_SEND * 2:
                continue  # Keep minimum garrison

            neighbors = self.game.graph.neighbors(node)
            if not neighbors:
                continue

            # Compute attention differences
            self_attention = self.attention[node]
            nbr_attention_diff = np.array([
                self.attention[nbr] - self_attention for nbr in neighbors
            ])

            # Positive part only (flow toward higher attention)
            nbr_attention_pos = np.maximum(nbr_attention_diff, 0)
            norm_pos = np.linalg.norm(nbr_attention_pos)

            if norm_pos < 1e-6:
                continue  # No positive gradient, no flow

            # Total outflow rate: softmax of the norm
            # softmax(x) = exp(x) / (1 + exp(x)) for scalar
            # Clamp to prevent overflow
            clamped_norm = min(norm_pos, 20.0)
            outflow_fraction = np.exp(clamped_norm) / (1 + np.exp(clamped_norm))
            outflow_fraction *= OUTFLOW_RATE

            # Prorate by contribution
            allocation = nbr_attention_pos / norm_pos

            # Compute actual troops to send
            total_to_send = troops_here * outflow_fraction
            if total_to_send < MIN_TROOPS_TO_SEND:
                continue

            # Send to each neighbor proportionally
            for i, nbr in enumerate(neighbors):
                amount = total_to_send * allocation[i]
                if amount >= MIN_TROOPS_TO_SEND:
                    success = self.game.send_troops(node, nbr, self.player, amount)
                    if success:
                        # Record gradient sign for retreat detection
                        # (will be picked up next tick in _handle_retreats)
                        pass

    def _try_build_structures(self) -> None:
        """
        Try to build factories/powerplants on owned nodes.

        Heuristics:
        - Build factory on interior nodes (no enemy neighbors)
        - Build powerplant if adjacent to 2+ factories
        """
        from risky_graph import COST_FACTORY, COST_POWERPLANT

        my_nodes = self.game.get_player_nodes(self.player)

        for node in my_nodes:
            state = self.game.get_node_state(node)
            #if state != NodeState.DEFAULT:
             #   continue

            troops = self.game.get_node_troops(node)[self.player]
            neighbors = self.game.graph.neighbors(node)

            # Check if interior (all neighbors owned by us)
            is_interior = True # all(
            #    self.game.get_node_owner(nbr) == self.player
            #    for nbr in neighbors
            #)

            # Count adjacent factories/capitals
            adjacent_production = sum(
                1 for nbr in neighbors
                if (self.game.get_node_owner(nbr) == self.player and
                    (self.game.get_node_state(nbr) == NodeState.FACTORY or
                     self.game.is_capital(nbr)))
            )

            adjacent_powerplants = sum(
                1 for nbr in neighbors 
                if (self.game.get_node_owner(nbr) == self.player and 
                    self.game.get_node_state(nbr) == NodeState.POWERPLANT)
            )

            factory_pp_balance = adjacent_production - adjacent_powerplants

            # Decision
            if is_interior and troops > COST_POWERPLANT and state != NodeState.POWERPLANT and factory_pp_balance > 0:
                # Good powerplant location
                self.game.set_node_state(node, NodeState.POWERPLANT, self.player)
            elif is_interior and troops > COST_FACTORY and state != NodeState.FACTORY and ((not state == NodeState.POWERPLANT) or factory_pp_balance < 0):
                # Build factory
                self.game.set_node_state(node, NodeState.FACTORY, self.player)
