#ifndef CRISKY_OT_SOLVER_HPP
#define CRISKY_OT_SOLVER_HPP

#include "engine/graph.hpp"
#include "engine/player_interface.hpp"
#include "systems/transport/transport_solvers.hpp"

#include <vector>

// All-pairs shortest path data for the graph.
// Precomputed once; O(N²) storage, O(N·E·log N) construction.
struct ShortestPathData {
    int N;
    std::vector<float> dist;  // N*N flat: dist[i*N+j] = shortest path i→j
    std::vector<int> pred;    // N*N flat: pred[i*N+j] = predecessor of j on path from i
};

// Compute all-pairs shortest paths using Dijkstra from each node.
ShortestPathData compute_shortest_paths(const Graph& graph);

// Min-cost flow transport solver using Successive Shortest Paths.
//
// Precomputes all-pairs shortest paths once. Each tick:
//   1. Compute supply/demand from troop state vs target
//   2. SSP: assign supply→demand in order of cheapest distance
//   3. Trace paths via predecessor array → TroopCommands
//
// Optimal (solves the transportation LP exactly). O(N) working data per tick.
// Edges are uncapacitated, so shortest paths are fixed — no residual graph needed.
class OTSolver {
public:
    OTSolver(const Graph& graph, const std::vector<int>& target_troops);
    OTSolver(const Graph& graph, const std::vector<int>& target_troops,
             const ShortestPathData& precomputed_sp);

    // Solve min-cost transport.  Optional demand_value (per-node, size N):
    // when provided, supply→demand edge cost becomes dist / value, making
    // high-value targets cheaper to serve.  Nodes with value <= 0 are skipped.
    std::vector<TroopCommand> solve(
        const std::vector<NodeData>& nodes,
        int player_id,
        const std::vector<bool>& masked,
        const std::vector<float>& demand_value = {});

    const ShortestPathData& shortest_paths() const { return sp_; }

    // Per-node Voronoi assignment from the last solve().
    // assignment[i] = demand node that most flow through node i serves.
    // -1 if no flow touches this node. Updated each solve().
    const std::vector<int>& last_voronoi() const { return voronoi_; }

private:
    int N_;
    const Graph* graph_;
    std::vector<int> target_;
    ShortestPathData sp_;
    std::vector<int> voronoi_;
};

// Factory: returns a TransportSolver closure wrapping OTSolver.
TransportSolver make_ot_solver(const Graph& graph, const std::vector<int>& target_troops);

#endif
