"""
RiskyGraph: Core game state management for Risky Strats.

Extends the Graph class to manage game-specific data and mechanics including
node states, troop vectors, traveling troops, and per-tick updates.

================================================================================
INDUCED GRAPH API
================================================================================
RiskyGraph assumes the following interface from the Graph class:

Properties:
    num_nodes: int
        Number of nodes in the graph.

    positions: np.ndarray
        Array of shape (num_nodes, 2) containing (x, y) coordinates for each node.

Methods:
    neighbors(node: int) -> list[int]
        Return list of node indices adjacent to the given node.

    subgraph(nodes: list[int]) -> "Graph"
        Return a new Graph containing only the specified nodes and edges between them.

    are_neighbors(node_a: int, node_b: int) -> bool
        Return True if node_a and node_b are adjacent.

    distance(node_a: int, node_b: int) -> float
        Return Euclidean distance between two nodes (||pos[a] - pos[b]||).
================================================================================
"""

from enum import Enum
from dataclasses import dataclass
from typing import Optional
import numpy as np

from graph import Graph


class NodeState(Enum):
    """Possible states for a node on the game board."""
    DEFAULT = "default"
    FACTORY = "factory"
    POWERPLANT = "powerplant"
    FORT = "fort"
    ARTILLERY = "artillery"


@dataclass
class TravelingTroops:
    """
    Represents a group of troops in transit between nodes.

    Troops travel via local hops determined by a greedy heuristic:
    the next local node is chosen to maximize dot product similarity
    between (local_to - from) and (global_to - from), normalized.

    Attributes:
        from_node: Index of the node troops were most recently sent from.
        local_to_node: Index of the immediate destination node (one hop).
        global_to_node: Index of the final destination node.
        t_sent: Game tick when this travel segment began.
        t_arrival: Game tick when troops arrive at local_to_node.
        num_troops: Number of troops in this group.
        owner: Player index who owns these troops.
    """
    from_node: int
    local_to_node: int
    global_to_node: int
    t_sent: float
    t_arrival: float
    num_troops: float
    owner: int


# Building costs (in troops)
COST_FACTORY = 500
COST_POWERPLANT = 2501
COST_FORT = 400
COST_ARTILLERY = 4000

# Combat parameters
ATTACK_RATIO = 1 / 100      # Attack = troops * ATTACK_RATIO
DEFENSE_RATIO = 1 / 1000    # Defense = troops * DEFENSE_RATIO
FORT_DEFENSE_BONUS = 0.5    # 50% defense bonus for forts
ARTILLERY_ATTACK_BONUS = 0.25  # 25% attack bonus per neighboring artillery

# Troop generation
CAPITAL_GENERATION = 2      # Troops per tick for capitals
FACTORY_GENERATION = 1      # Troops per tick for factories
POWERPLANT_BONUS = 2        # Additional troops per tick for adjacent factories/capitals

# Travel parameters
TRAVEL_SPEED_CONSTANT = 5.0  # Q - multiplier for travel time (higher = slower)
TRAVEL_TIME_MIN = 10.0       # Minimum travel time (ticks)


def troop_radius(num_troops: float) -> float:
    """
    Compute the radius of a troop group in graph units.

    This is THE canonical radius used for both:
    - Aggregation/merging (troops merge when radii touch)
    - Visual rendering (scatter size derived from this)

    Adjust this function to change troop size everywhere.
    """
    return 0.08 + 0.025 * np.log1p(num_troops)

# Initial troops at capital
STARTING_TROOPS = 100

# Neutral garrison at each non-capital node
NEUTRAL_GARRISON = 25


class RiskyGraph:
    """
    Game state manager for Risky Strats.

    Extends Graph to track node states, troop distributions, traveling troops,
    and provides methods for game actions and per-tick updates.

    Attributes:
        graph: Underlying Graph object defining topology and node positions.
        num_players: Number of players in the game.
        capitals: List of node indices, one per player (their starting node).
        player_alive: Boolean array indicating if each player is still in the game.
        current_tick: Current game time (discrete ticks).

    Node Data (per node):
        - state: NodeState enum
        - troops: np.ndarray of shape (num_players,), non-negative floats
        - owner: Optional[int], the player index if troops is scalar multiple of e_i, else None

    Traveling Troops:
        - List of TravelingTroops currently in transit.
    """

    def __init__(self, graph: Graph, capitals: list[int]):
        """
        Initialize a RiskyGraph from a Graph and list of capital nodes.

        Args:
            graph: A Graph object defining the game board topology and positions.
            capitals: List of node indices, one per player. The i-th entry is
                      player i's starting capital node. Length determines num_players.

        Raises:
            ValueError: If capitals contains duplicate nodes or invalid indices.
        """
        # Validate capitals
        if len(capitals) != len(set(capitals)):
            raise ValueError("Capitals must be unique nodes")
        for cap in capitals:
            if cap < 0 or cap >= graph.num_nodes:
                raise ValueError(f"Invalid capital node index: {cap}")

        self.graph = graph
        self.num_players = len(capitals)
        self.capitals = list(capitals)
        self.player_alive = np.ones(self.num_players, dtype=bool)
        self.current_tick = 0.0

        # Node data: state for each node
        self._node_states: list[NodeState] = [NodeState.DEFAULT] * graph.num_nodes

        # Node data: troops[node] is array of shape (num_players,)
        self._node_troops: np.ndarray = np.zeros(
            (graph.num_nodes, self.num_players), dtype=float
        )

        # Initialize capitals with starting troops
        for player, cap in enumerate(capitals):
            self._node_troops[cap, player] = STARTING_TROOPS

        # Track last genuine (uncontested) owner for fort bonus
        # None means never owned, int means last player to own it uncontested
        self._last_owner: list[Optional[int]] = [None] * graph.num_nodes
        for player, cap in enumerate(capitals):
            self._last_owner[cap] = player

        # Neutral garrison at each node (must be defeated to capture)
        # Capitals start with no garrison
        self._garrison = np.full(graph.num_nodes, NEUTRAL_GARRISON, dtype=float)
        for cap in capitals:
            self._garrison[cap] = 0

        # Traveling troops list
        self._traveling_troops: list[TravelingTroops] = []

    # =========================================================================
    # Node Data Getters
    # =========================================================================

    def get_node_state(self, node: int) -> NodeState:
        """Return the state of the given node."""
        return self._node_states[node]

    def get_node_troops(self, node: int) -> np.ndarray:
        """
        Return the troop vector for the given node.

        Returns:
            np.ndarray of shape (num_players,) with troop counts per player.
        """
        return self._node_troops[node].copy()

    def get_node_owner(self, node: int) -> Optional[int]:
        """
        Return the owner of the given node, or None if contested/empty/garrisoned.

        A node is owned by player i iff:
        - Its garrison is depleted (0)
        - Its troop vector is a scalar multiple of e_i (only player i has troops)
        """
        # Node with active garrison is not owned by any player
        if self._garrison[node] > 0:
            return None

        troops = self._node_troops[node]
        nonzero = np.where(troops > 0)[0]
        if len(nonzero) == 1:
            return int(nonzero[0])
        return None

    def is_capital(self, node: int) -> bool:
        """Return True if the node is any player's capital."""
        return node in self.capitals

    def get_capital_owner(self, node: int) -> Optional[int]:
        """Return the player index if node is a capital, else None."""
        if node in self.capitals:
            return self.capitals.index(node)
        return None

    def get_garrison(self, node: int) -> float:
        """Return the neutral garrison troops at a node."""
        return self._garrison[node]

    # =========================================================================
    # Node Data Setters
    # =========================================================================

    def set_node_state(self, node: int, state: NodeState, player: int) -> bool:
        """
        Attempt to convert a node to a new state.

        Deducts the appropriate troop cost from the player's troops at that node.
        Fails if the player doesn't own the node or lacks sufficient troops.

        Args:
            node: Node index to convert.
            state: Target NodeState.
            player: Player index attempting the conversion.

        Returns:
            True if conversion succeeded, False otherwise.
        """
        # Check ownership
        if self.get_node_owner(node) != player:
            return False

        # Determine cost
        cost_map = {
            NodeState.DEFAULT: 0,
            NodeState.FACTORY: COST_FACTORY,
            NodeState.POWERPLANT: COST_POWERPLANT,
            NodeState.FORT: COST_FORT,
            NodeState.ARTILLERY: COST_ARTILLERY,
        }
        cost = cost_map.get(state, 0)

        # Check sufficient troops
        if self._node_troops[node, player] < cost:
            return False

        # Deduct cost and set state
        self._node_troops[node, player] -= cost
        self._node_states[node] = state
        return True

    # =========================================================================
    # Troop Movement
    # =========================================================================

    def _choose_next_hop(self, from_node: int, global_to_node: int) -> int:
        """
        Choose the next local hop toward a global destination.

        Uses greedy dot-product heuristic: picks the neighbor that maximizes
        the dot product between (neighbor - from) and (global_to - from),
        both normalized.

        TODO: Implement cover-based hierarchical pathfinding for non-convex graphs.
        The approach:
        1. Partition R² into grid cells (covers) of size ~threshold
        2. Build cover graph where cells are adjacent if nodes between them share edges
        3. TravelingTroops stores cover_path: list[CellID] + cover_index: int
        4. Within a cover, use dot-product toward centroid of next cover
        5. When entering new cover, increment index

        Args:
            from_node: Current node.
            global_to_node: Final destination.

        Returns:
            Node index of the best next hop. Returns global_to_node if it's a neighbor.
        """
        neighbors = self.graph.neighbors(from_node)

        # If destination is a neighbor, go directly
        if global_to_node in neighbors:
            return global_to_node

        from_pos = self.graph.positions[from_node]
        global_to_pos = self.graph.positions[global_to_node]
        global_dir = global_to_pos - from_pos
        global_dir_norm = np.linalg.norm(global_dir)
        if global_dir_norm > 0:
            global_dir = global_dir / global_dir_norm

        best_neighbor = neighbors[0]
        best_dot = -np.inf

        for neighbor in neighbors:
            neighbor_pos = self.graph.positions[neighbor]
            local_dir = neighbor_pos - from_pos
            local_norm = np.linalg.norm(local_dir)
            if local_norm > 0:
                local_dir = local_dir / local_norm
            dot = np.dot(local_dir, global_dir)
            if dot > best_dot:
                best_dot = dot
                best_neighbor = neighbor

        return best_neighbor

    def _compute_travel_time(self, from_node: int, to_node: int, num_troops: float) -> float:
        """Compute travel time for a segment."""
        dist = self.graph.distance(from_node, to_node)
        return max(dist * np.log1p(num_troops) * TRAVEL_SPEED_CONSTANT, TRAVEL_TIME_MIN)

    def send_troops(
        self,
        from_node: int,
        to_node: int,
        player: int,
        num_troops: float
    ) -> bool:
        """
        Send troops from one node toward another.

        Initiates travel from from_node toward to_node. Troops will path via
        local hops using the greedy dot-product heuristic. If troops from a
        recent send are within touching distance of from_node, the sends
        are merged rather than creating a new TravelingTroops object.

        Travel time for a segment is: max(||p - q|| * log(1 + troops) * Q, TRAVEL_TIME_MIN)

        Args:
            from_node: Node to send troops from.
            to_node: Final destination node (global target).
            player: Player index sending troops.
            num_troops: Number of troops to send (must be <= troops available).

        Returns:
            True if troops were successfully dispatched, False otherwise.
        """
        # Validate
        if num_troops <= 0:
            return False
        if self._node_troops[from_node, player] < num_troops:
            return False
        if from_node == to_node:
            return False

        # Check for aggregation: find existing troops near from_node going same direction
        from_pos = self.graph.positions[from_node]
        new_troop_radius = troop_radius(num_troops)
        for troop in self._traveling_troops:
            if (troop.owner == player and
                troop.global_to_node == to_node and
                troop.from_node == from_node):
                # Check if radii would touch
                troop_pos = self.get_troop_position(troop)
                dist = np.linalg.norm(np.array(troop_pos) - from_pos)
                if dist <= troop_radius(troop.num_troops) + new_troop_radius:
                    # Aggregate: add troops to existing group
                    self._node_troops[from_node, player] -= num_troops
                    troop.num_troops += num_troops
                    # Recalculate arrival time based on new troop count
                    new_travel_time = self._compute_travel_time(
                        troop.from_node, troop.local_to_node, troop.num_troops
                    )
                    troop.t_arrival = troop.t_sent + new_travel_time
                    return True

        # No aggregation, create new traveling troops
        local_to = self._choose_next_hop(from_node, to_node)
        travel_time = self._compute_travel_time(from_node, local_to, num_troops)

        self._node_troops[from_node, player] -= num_troops
        self._traveling_troops.append(TravelingTroops(
            from_node=from_node,
            local_to_node=local_to,
            global_to_node=to_node,
            t_sent=self.current_tick,
            t_arrival=self.current_tick + travel_time,
            num_troops=num_troops,
            owner=player
        ))
        return True

    def retreat_troops(self, traveling_troops: TravelingTroops) -> None:
        """
        Retreat a group of traveling troops.

        Troops return to their from_node from their current position.
        Travel time is recalculated based on distance remaining and troop count.

        Args:
            traveling_troops: The TravelingTroops object to retreat.
        """
        if traveling_troops not in self._traveling_troops:
            return

        # Calculate current progress (0 to 1) along the edge
        total_time = traveling_troops.t_arrival - traveling_troops.t_sent
        if total_time <= 0:
            old_progress = 1.0
        else:
            elapsed = self.current_tick - traveling_troops.t_sent
            old_progress = np.clip(elapsed / total_time, 0.0, 1.0)

        # Remove from list
        self._traveling_troops.remove(traveling_troops)

        # If barely started, just snap back to from_node
        if old_progress < 0.01:
            self._node_troops[traveling_troops.from_node, traveling_troops.owner] += traveling_troops.num_troops
            return

        # Compute retreat time based on distance to travel back and troop count
        edge_length = self.graph.distance(traveling_troops.from_node, traveling_troops.local_to_node)
        dist_to_retreat = old_progress * edge_length
        retreat_time = max(
            dist_to_retreat * np.log1p(traveling_troops.num_troops) * TRAVEL_SPEED_CONSTANT,
            TRAVEL_TIME_MIN
        )

        # Set t_sent so that at current_tick, progress along the reversed edge = 1 - old_progress
        # This makes the troop appear at its current position on the new (reversed) edge
        if old_progress >= 0.99:
            # Essentially at destination, start retreat from there
            new_t_sent = self.current_tick
        else:
            # Calculate t_sent in the past so progress formula gives correct position
            new_t_sent = self.current_tick - retreat_time * (1 - old_progress) / old_progress

        new_t_arrival = self.current_tick + retreat_time

        # Create new traveling troops going back
        self._traveling_troops.append(TravelingTroops(
            from_node=traveling_troops.local_to_node,
            local_to_node=traveling_troops.from_node,
            global_to_node=traveling_troops.from_node,
            t_sent=new_t_sent,
            t_arrival=new_t_arrival,
            num_troops=traveling_troops.num_troops,
            owner=traveling_troops.owner
        ))

    # =========================================================================
    # Bulk Operations
    # =========================================================================

    def send_from_nodes(
        self,
        nodes: set[int],
        player: int,
        target_node: int,
        amount: float,
        is_percentage: bool = False
    ) -> float:
        """
        Send troops from a set of owned nodes to a target.

        For each node in the set owned by the player, sends the specified
        amount (or percentage) of troops toward target_node.

        Args:
            nodes: Set of node indices to send from.
            player: Player index whose troops to send.
            target_node: Destination node index.
            amount: Number of troops per node, or percentage if is_percentage=True.
            is_percentage: If True, amount is interpreted as a percentage (0-100).

        Returns:
            Total number of troops dispatched across all nodes.
        """
        total_sent = 0.0
        for node in nodes:
            if self.get_node_owner(node) != player:
                continue
            if node == target_node:
                continue

            available = self._node_troops[node, player]
            if is_percentage:
                to_send = available * (amount / 100.0)
            else:
                to_send = min(amount, available)

            if to_send > 0 and self.send_troops(node, target_node, player, to_send):
                total_sent += to_send

        return total_sent

    def retreat_traveling_troops(
        self,
        troops: list[TravelingTroops]
    ) -> int:
        """
        Retreat a list of traveling troop groups.

        Args:
            troops: List of TravelingTroops to retreat.

        Returns:
            Number of groups successfully retreated.
        """
        count = 0
        # Make a copy since retreat_troops modifies the list
        for troop in list(troops):
            if troop in self._traveling_troops:
                self.retreat_troops(troop)
                count += 1
        return count

    # =========================================================================
    # Spatial Queries
    # =========================================================================

    def get_nodes_in_radius(
        self,
        center: tuple[float, float],
        radius: float
    ) -> set[int]:
        """
        Get all node indices within a radius of a point.

        Args:
            center: (x, y) coordinates.
            radius: Selection radius.

        Returns:
            Set of node indices within the radius.
        """
        center_arr = np.array(center)
        result = set()
        for i in range(self.graph.num_nodes):
            dist = np.linalg.norm(self.graph.positions[i] - center_arr)
            if dist <= radius:
                result.add(i)
        return result

    def get_traveling_troops_in_radius(
        self,
        center: tuple[float, float],
        radius: float,
        player: Optional[int] = None
    ) -> list[TravelingTroops]:
        """
        Get traveling troops whose current position is within a radius.

        The current position is interpolated based on travel progress.

        Args:
            center: (x, y) coordinates.
            radius: Selection radius.
            player: If provided, only return troops owned by this player.

        Returns:
            List of TravelingTroops within the radius.
        """
        center_arr = np.array(center)
        result = []
        for troop in self._traveling_troops:
            if player is not None and troop.owner != player:
                continue
            pos = self.get_troop_position(troop)
            dist = np.linalg.norm(np.array(pos) - center_arr)
            if dist <= radius:
                result.append(troop)
        return result

    def get_troop_position(self, troop: TravelingTroops) -> tuple[float, float]:
        """
        Get the current interpolated position of traveling troops.

        Position is linearly interpolated between from_node and local_to_node
        based on (current_tick - t_sent) / (t_arrival - t_sent).

        Args:
            troop: A TravelingTroops object.

        Returns:
            (x, y) coordinates of current position.
        """
        from_pos = self.graph.positions[troop.from_node]
        to_pos = self.graph.positions[troop.local_to_node]

        # Calculate interpolation factor
        total_time = troop.t_arrival - troop.t_sent
        if total_time <= 0:
            return tuple(to_pos)

        elapsed = self.current_tick - troop.t_sent
        t = np.clip(elapsed / total_time, 0.0, 1.0)

        pos = from_pos + t * (to_pos - from_pos)
        return (float(pos[0]), float(pos[1]))

    # =========================================================================
    # Game Tick Update
    # =========================================================================

    def update(self) -> None:
        """
        Advance the game state by one tick.

        Performs updates in order:
        1. Aggregate traveling troops
        2. Handle opposing troop collisions
        3. Update traveling troops (arrivals, position updates)
        4. Update battles (resolve combat on contested nodes)
        5. Update factories (generate troops for owned production nodes)
        6. Check player alive status
        7. Increment current_tick
        """
        self._aggregate_traveling_troops()
        self._handle_opposing_collisions()
        self._update_traveling_troops()
        self._update_battles()
        self._update_factories()
        self._update_player_status()
        self.current_tick += 1

    def _handle_opposing_collisions(self) -> None:
        """
        Handle collisions between opposing troops on the same edge.

        When troops from different players travel in opposite directions on
        the same edge and get close enough, the smaller group retreats.
        """
        from collections import defaultdict

        if not self._traveling_troops:
            return

        # Group troops by edge (unordered node pair)
        edge_troops: dict[tuple[int, int], list[TravelingTroops]] = defaultdict(list)
        for troop in self._traveling_troops:
            edge = tuple(sorted([troop.from_node, troop.local_to_node]))
            edge_troops[edge].append(troop)

        to_retreat = []

        for edge, troops in edge_troops.items():
            if len(troops) < 2:
                continue

            # Group by owner and direction
            # direction: True if going from edge[0] to edge[1]
            groups: dict[tuple[int, bool], list[TravelingTroops]] = defaultdict(list)
            for troop in troops:
                direction = troop.from_node == edge[0]
                groups[(troop.owner, direction)].append(troop)

            # Find opposing groups (different owners, opposite directions)
            owners_directions = list(groups.keys())
            for idx_i, (owner_i, dir_i) in enumerate(owners_directions):
                for owner_j, dir_j in owners_directions[idx_i + 1:]:
                    if owner_i != owner_j and dir_i != dir_j:
                        # Opposing groups found - check for collision
                        group_i = groups[(owner_i, dir_i)]
                        group_j = groups[(owner_j, dir_j)]

                        # Check if any troops are close enough to collide
                        collision = False
                        for ti in group_i:
                            pos_i = np.array(self.get_troop_position(ti))
                            radius_i = troop_radius(ti.num_troops)
                            for tj in group_j:
                                pos_j = np.array(self.get_troop_position(tj))
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
                                to_retreat.extend(group_i)
                            elif total_j < total_i:
                                to_retreat.extend(group_j)
                            # Equal: both retreat
                            else:
                                to_retreat.extend(group_i)
                                to_retreat.extend(group_j)

        # Execute retreats
        for troop in to_retreat:
            if troop in self._traveling_troops:
                self.retreat_troops(troop)

    def _aggregate_traveling_troops(self) -> None:
        """
        Aggregate traveling troops mid-journey.

        Troops on the same edge (same from/to/owner/destination) that are
        within epsilon + radii of each other merge into the furthest-along
        troop. Uses rolling window: if A↔B↔C are each pairwise within threshold,
        all three merge even if A↔C > threshold.

        Uses 1D projection along edge for efficiency.
        """
        if not self._traveling_troops:
            return

        from collections import defaultdict

        # Group by (from_node, local_to_node, owner, global_to_node)
        groups: dict[tuple, list[TravelingTroops]] = defaultdict(list)
        for troop in self._traveling_troops:
            key = (troop.from_node, troop.local_to_node, troop.owner, troop.global_to_node)
            groups[key].append(troop)

        to_remove = set()

        for key, troops in groups.items():
            if len(troops) < 2:
                continue

            # Get edge length for 1D projection
            edge_length = self.graph.distance(key[0], key[1])
            if edge_length <= 0:
                continue

            # Compute progress (0 to 1) for each troop
            def get_progress(t: TravelingTroops) -> float:
                total_time = t.t_arrival - t.t_sent
                if total_time <= 0:
                    return 1.0
                elapsed = self.current_tick - t.t_sent
                return min(1.0, max(0.0, elapsed / total_time))

            # Sort by progress (furthest along last)
            troops_with_progress = [(t, get_progress(t)) for t in troops]
            troops_with_progress.sort(key=lambda x: x[1])

            # Rolling window clustering using 1D distance along edge
            n = len(troops_with_progress)
            cluster_id = list(range(n))  # Union-find parent

            def find(i: int) -> int:
                if cluster_id[i] != i:
                    cluster_id[i] = find(cluster_id[i])
                return cluster_id[i]

            def union(i: int, j: int) -> None:
                ri, rj = find(i), find(j)
                if ri != rj:
                    # Always point to the higher index (furthest along)
                    cluster_id[ri] = rj

            # Check adjacent pairs in sorted order
            for i in range(n - 1):
                t_i, prog_i = troops_with_progress[i]
                t_j, prog_j = troops_with_progress[i + 1]

                # 1D distance along edge
                dist_1d = (prog_j - prog_i) * edge_length

                # Merge when radii touch (troop_radius is the single source of truth)
                threshold = troop_radius(t_i.num_troops) + troop_radius(t_j.num_troops)

                if dist_1d <= threshold:
                    union(i, i + 1)

            # Group by cluster root
            clusters: dict[int, list[int]] = defaultdict(list)
            for i in range(n):
                clusters[find(i)].append(i)

            # Merge each cluster into the furthest-along troop (highest index in sorted list)
            for root, members in clusters.items():
                if len(members) < 2:
                    continue

                # Furthest along is the max index (last in sorted order)
                leader_idx = max(members)
                leader = troops_with_progress[leader_idx][0]

                # Sum troops from all others into leader
                for idx in members:
                    if idx != leader_idx:
                        other = troops_with_progress[idx][0]
                        leader.num_troops += other.num_troops
                        to_remove.add(id(other))

        # Remove merged troops
        self._traveling_troops = [t for t in self._traveling_troops if id(t) not in to_remove]

    def _update_traveling_troops(self) -> None:
        """
        Process all traveling troops.

        For each TravelingTroops:
        - If t_arrival <= current_tick: troops arrive at local_to_node.
          - If local_to_node == global_to_node: add troops to node.
          - Else: initiate next hop via send_troops from local_to_node.
        - Else: troops remain in transit (no action needed).
        """
        arrived = []
        for troop in self._traveling_troops:
            if troop.t_arrival <= self.current_tick:
                arrived.append(troop)

        for troop in arrived:
            self._traveling_troops.remove(troop)

            if troop.local_to_node == troop.global_to_node:
                # Arrived at final destination
                self._node_troops[troop.local_to_node, troop.owner] += troop.num_troops
            else:
                # Arrived at intermediate node, continue to next hop
                # First add troops to the node temporarily
                self._node_troops[troop.local_to_node, troop.owner] += troop.num_troops
                # Then send them onward
                self.send_troops(
                    troop.local_to_node,
                    troop.global_to_node,
                    troop.owner,
                    troop.num_troops
                )

    def _update_battles(self) -> None:
        """
        Resolve combat on all contested nodes.

        Handles both player-vs-player combat and player-vs-garrison combat.
        Garrison must be depleted before node can be owned.
        """
        for node in range(self.graph.num_nodes):
            troops = self._node_troops[node].copy()
            garrison = self._garrison[node]
            nonzero_players = np.where(troops > 0)[0]

            # Skip if no troops present
            if len(nonzero_players) == 0:
                continue

            # Compute attack and defense vectors for players
            attack = troops * ATTACK_RATIO
            defense = troops * DEFENSE_RATIO

            # Apply fort bonus to the last genuine (uncontested) owner
            if self._node_states[node] == NodeState.FORT:
                fort_owner = self._last_owner[node]
                if fort_owner is not None and troops[fort_owner] > 0:
                    defense[fort_owner] *= (1 + FORT_DEFENSE_BONUS)

            # Apply artillery bonus from neighboring artilleries
            for neighbor in self.graph.neighbors(node):
                if self._node_states[neighbor] == NodeState.ARTILLERY:
                    artillery_owner = self.get_node_owner(neighbor)
                    if artillery_owner is not None:
                        attack[artillery_owner] *= (1 + ARTILLERY_ATTACK_BONUS)

            total_player_attack = np.sum(attack)

            # Garrison combat: garrison attacks all players, all players attack garrison
            if garrison > 0:
                garrison_attack = garrison * ATTACK_RATIO
                garrison_defense = garrison * DEFENSE_RATIO

                # Garrison damages all players present
                for player in nonzero_players:
                    loss = garrison_attack - defense[player]
                    if loss > 0:
                        self._node_troops[node, player] = max(0, troops[player] - loss)

                # All players damage garrison
                garrison_loss = total_player_attack - garrison_defense
                if garrison_loss > 0:
                    self._garrison[node] = max(0, garrison - garrison_loss)

            # Player-vs-player combat (only if multiple players and no/low garrison)
            if len(nonzero_players) > 1:
                for player in nonzero_players:
                    incoming_attack = total_player_attack - attack[player]
                    loss = incoming_attack - defense[player]
                    if loss > 0:
                        current = self._node_troops[node, player]
                        self._node_troops[node, player] = max(0, current - loss)

        # Update last_owner for any nodes that are now uncontested
        for node in range(self.graph.num_nodes):
            owner = self.get_node_owner(node)
            if owner is not None:
                self._last_owner[node] = owner

    def _update_factories(self) -> None:
        """
        Generate troops at production nodes.

        For each node owned by a player:
        - Capital: +CAPITAL_GENERATION troops
        - Factory: +FACTORY_GENERATION troops
        - If adjacent to owned powerplant: +POWERPLANT_BONUS additional troops

        Troops are added to the owner's component of the troop vector.
        """
        for node in range(self.graph.num_nodes):
            owner = self.get_node_owner(node)
            if owner is None:
                continue

            generation = 0.0
            state = self._node_states[node]

            # Base generation
            if self.is_capital(node):
                generation += CAPITAL_GENERATION
            if state == NodeState.FACTORY:
                generation += FACTORY_GENERATION

            # Powerplant bonus: check if this is a capital or factory adjacent to owned powerplants
            # Stacks for each adjacent powerplant
            if self.is_capital(node) or state == NodeState.FACTORY:
                for neighbor in self.graph.neighbors(node):
                    if (self._node_states[neighbor] == NodeState.POWERPLANT and
                        self.get_node_owner(neighbor) == owner):
                        generation += POWERPLANT_BONUS

            if generation > 0:
                self._node_troops[node, owner] += generation

    def _update_player_status(self) -> None:
        """
        Update player_alive flags.

        A player is eliminated if they own no nodes. Upon elimination,
        their traveling troops are destroyed.
        """
        for player in range(self.num_players):
            if not self.player_alive[player]:
                continue

            # Check if player owns any nodes
            owns_any = False
            for node in range(self.graph.num_nodes):
                if self.get_node_owner(node) == player:
                    owns_any = True
                    break

            if not owns_any:
                self.player_alive[player] = False
                # Destroy all traveling troops belonging to this player
                self._traveling_troops = [
                    t for t in self._traveling_troops if t.owner != player
                ]

    # =========================================================================
    # Queries
    # =========================================================================

    def get_traveling_troops(self, player: Optional[int] = None) -> list[TravelingTroops]:
        """
        Get all traveling troops, optionally filtered by player.

        Args:
            player: If provided, only return troops owned by this player.

        Returns:
            List of TravelingTroops objects.
        """
        if player is None:
            return list(self._traveling_troops)
        return [t for t in self._traveling_troops if t.owner == player]

    def get_player_nodes(self, player: int) -> list[int]:
        """Return list of node indices owned by the given player."""
        return [n for n in range(self.graph.num_nodes) if self.get_node_owner(n) == player]

    def get_visible_nodes(self, player: int) -> list[int]:
        """
        Return nodes visible to a player (for fog of war).

        A player can see nodes they own and all neighbors of owned nodes.
        """
        visible = set()
        for node in range(self.graph.num_nodes):
            if self.get_node_owner(node) == player:
                visible.add(node)
                for neighbor in self.graph.neighbors(node):
                    visible.add(neighbor)
        return list(visible)
