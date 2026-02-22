#ifndef CRISKY_NETWORK_SIMPLEX_HPP
#define CRISKY_NETWORK_SIMPLEX_HPP

#include "engine/graph.hpp"
#include "engine/player_interface.hpp"

#include <vector>

// Min-cost flow transport solver using Network Simplex on the game graph,
// with Frank-Wolfe decomposition for convex saturation costs.
//
// All arcs are preallocated once in the constructor — no realloc per tick:
//   [0, static_end_)                  game-graph arcs (2 per undirected edge)
//   [src_base_, src_base_+N)          SRC→node_i  (supply / producer, cap=0 when inactive)
//   [sink_base_, sink_base_+N)        node_i→SINK (demand, cap=0 when inactive)
//   [art_base_, art_base_+N+1)        artificial arcs (Big-M, for cold-start basis)
//
// Per-tick update just writes cap/cost on the preallocated slots.
class NetworkSimplex {
public:
    explicit NetworkSimplex(const Graph& game_graph);

    std::vector<TroopCommand> solve(
        const std::vector<NodeData>& nodes,
        int player_id,
        const std::vector<bool>& masked,
        const std::vector<int>& targets,
        const std::vector<float>& demand_value = {},
        const std::vector<float>& effective_troops = {},
        const std::vector<float>& production_rate = {},
        float saturation_alpha = 0.005f,
        float value_alpha = 0.0f,
        int fw_iterations = 8);

    const std::vector<int>& last_voronoi() const { return voronoi_; }
    int last_pivot_count() const { return last_pivot_count_; }
    bool last_was_warm() const { return last_was_warm_; }
    int last_fw_iterations() const { return last_fw_iterations_; }

private:
    struct Arc {
        int from, to;
        int cap;
        float cost;
        int flow;
    };

    int N_;
    int num_nodes_;       // N + 2 (SRC, SINK)
    int SRC_, SINK_;
    const Graph* graph_;

    std::vector<Arc> arcs_;
    int static_end_;      // game arcs end here
    int src_base_;        // SRC→i arcs start here (N arcs)
    int sink_base_;       // i→SINK arcs start here (N arcs)
    int art_base_;        // artificial arcs start here (N+1 arcs)

    // Basis tree (indexed by node)
    std::vector<int> parent_;
    std::vector<int> parent_arc_;
    std::vector<float> potential_;
    std::vector<int> depth_;
    std::vector<int> thread_;

    bool has_basis_ = false;
    int last_pivot_count_ = 0;
    bool last_was_warm_ = false;
    int last_fw_iterations_ = 0;
    std::vector<int> voronoi_;

    // Producer arc tracking (for FW cost updates)
    std::vector<int> producer_arc_indices_;
    std::vector<int> producer_node_indices_;

    void build_static_arcs();
    void update_dynamic_arcs(const std::vector<int>& supply, const std::vector<int>& demand,
                             const std::vector<float>& demand_val, float value_alpha,
                             const std::vector<float>& production_rate,
                             const std::vector<bool>& masked, int player_id,
                             const std::vector<NodeData>& nodes);
    void initialize_artificial_basis();
    void run_simplex();
    void recompute_potentials();
    int find_entering_arc();
    int find_lca(int u, int v);
    void pivot(int entering);
    void extract_voronoi(const std::vector<int>& demand_nodes);
    std::vector<TroopCommand> extract_commands(
        const std::vector<NodeData>& nodes, int player_id,
        const std::vector<bool>& masked);
};

#endif
