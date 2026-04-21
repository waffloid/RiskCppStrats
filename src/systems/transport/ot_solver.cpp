#include "systems/transport/ot_solver.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <utility>
#include <vector>

// ============================================================================
// All-pairs shortest paths (Dijkstra from each node)
// ============================================================================

ShortestPathData compute_shortest_paths(const Graph& graph) {
    int N = graph.num_nodes();
    ShortestPathData sp;
    sp.N = N;
    sp.dist.assign(N * N, std::numeric_limits<float>::infinity());
    sp.pred.assign(N * N, -1);

    // Build adjacency list with weights
    struct AdjEntry { int to; float weight; };
    std::vector<std::vector<AdjEntry>> adj(N);
    for (const auto& e : graph.edges) {
        adj[e.a_idx].push_back({e.b_idx, e.length});
        adj[e.b_idx].push_back({e.a_idx, e.length});
    }

    using PQEntry = std::pair<float, int>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    for (int src = 0; src < N; src++) {
        float* d = &sp.dist[src * N];
        int* p = &sp.pred[src * N];

        d[src] = 0.0f;
        pq.push({0.0f, src});

        while (!pq.empty()) {
            auto [dist_u, u] = pq.top();
            pq.pop();
            if (dist_u > d[u]) continue;

            for (const auto& [v, w] : adj[u]) {
                float nd = d[u] + w;
                if (nd < d[v]) {
                    d[v] = nd;
                    p[v] = u;
                    pq.push({nd, v});
                }
            }
        }
    }

    return sp;
}

// ============================================================================
// OTSolver
// ============================================================================

OTSolver::OTSolver(const Graph& graph, const std::vector<int>& target_troops)
    : N_(graph.num_nodes())
    , graph_(&graph)
    , target_(target_troops)
    , sp_(compute_shortest_paths(graph))
    , voronoi_(graph.num_nodes(), -1) {}

OTSolver::OTSolver(const Graph& graph, const std::vector<int>& target_troops,
                   const ShortestPathData& precomputed_sp)
    : N_(graph.num_nodes())
    , graph_(&graph)
    , target_(target_troops)
    , sp_(precomputed_sp)
    , voronoi_(graph.num_nodes(), -1) {}

std::vector<TroopCommand> OTSolver::solve(
    const std::vector<NodeData>& nodes,
    int player_id,
    const std::vector<bool>& masked,
    const std::vector<float>& demand_value,
    const std::vector<float>& effective_troops,
    const std::vector<float>& production_rate,
    int saturation_window,
    float saturation_cost_scale) {

    bool has_effective = !effective_troops.empty();

    // Compute supply and demand.
    // Supply uses node troops (only send what's physically present).
    // Demand uses effective troops when available (includes in-transit),
    // preventing overallocation to nodes that already have troops en route.
    std::vector<int> supply(N_, 0);
    std::vector<int> demand(N_, 0);
    for (int i = 0; i < N_; i++) {
        int current_node = nodes[i].troops[player_id];
        int current_effective = has_effective
            ? static_cast<int>(effective_troops[i])
            : current_node;
        int target = target_[i];
        if (current_node > target) {
            supply[i] = current_node - target;
        } else if (target > current_effective) {
            demand[i] = target - current_effective;
        }
    }

    // Zero out supply from masked nodes
    for (int i = 0; i < N_; i++) {
        if (masked[i]) supply[i] = 0;
    }

    // Collect supply and demand node indices
    struct SupplyNode { int idx; int remaining; };
    struct DemandNode { int idx; int remaining; };
    std::vector<SupplyNode> suppliers;
    std::vector<DemandNode> demanders;
    for (int i = 0; i < N_; i++) {
        if (supply[i] > 0) suppliers.push_back({i, supply[i]});
        if (demand[i] > 0) demanders.push_back({i, demand[i]});
    }

    if (demanders.empty()) {
        voronoi_.assign(N_, -1);
        return {};
    }

    // ========================================================================
    // Saturation tranches: add future production capacity.
    //
    // At-target producing nodes (surplus == 0) become new 0-supply suppliers.
    // Each producing supplier gets parallel SRC→supply edges with increasing
    // cost reflecting wait time for future production.
    // ========================================================================
    bool has_saturation = !production_rate.empty() && saturation_window > 0;

    if (has_saturation) {
        // Add at-target producing nodes as new 0-supply suppliers
        for (int i = 0; i < N_; i++) {
            if (masked[i]) continue;
            if (production_rate[i] <= 0.0f) continue;
            if (supply[i] > 0) continue;  // already a supplier
            if (demand[i] > 0) continue;   // it's a demander
            if (nodes[i].owner != player_id) continue;
            // At target, has production → add as 0-supply supplier
            suppliers.push_back({i, 0});
        }
    }

    if (suppliers.empty()) {
        voronoi_.assign(N_, -1);
        return {};
    }

    // ========================================================================
    // Min-cost flow via SSP on bipartite supply-demand network.
    //
    // Nodes: 0 = super_source, 1..S = supply, S+1..S+D = demand, S+D+1 = super_sink
    // Edges: source→supply (cap=supply_amount, cost=0),
    //        supply→demand (cap=∞, cost=dist),
    //        demand→sink (cap=demand_amount, cost=0)
    //
    // SSP with Dijkstra + Johnson potentials finds optimal min-cost max-flow.
    // ========================================================================

    int S = static_cast<int>(suppliers.size());
    int D = static_cast<int>(demanders.size());
    int V = S + D + 2;
    int SRC = 0, SINK = V - 1;

    struct FlowEdge {
        int to, cap;
        float cost;
        int rev;  // index of reverse edge in adj[to]
    };

    std::vector<std::vector<FlowEdge>> adj(V);

    auto add_mcf_edge = [&](int u, int v, int cap, float cost) {
        adj[u].push_back({v, cap, cost, static_cast<int>(adj[v].size())});
        adj[v].push_back({u, 0, -cost, static_cast<int>(adj[u].size()) - 1});
    };

    // Source → supply nodes (tranche 0: free existing troops)
    for (int i = 0; i < S; i++) {
        if (suppliers[i].remaining > 0) {
            add_mcf_edge(SRC, 1 + i, suppliers[i].remaining, 0.0f);
        }
    }

    // Future production tranches
    if (has_saturation) {
        int total_demand = 0;
        for (int j = 0; j < D; j++) total_demand += demanders[j].remaining;

        int total_supply = 0;
        for (int i = 0; i < S; i++) total_supply += suppliers[i].remaining;

        // Add tranches until supply >= demand (or max 10 tranches to bound)
        for (int k = 1; total_supply < total_demand && k <= 10; k++) {
            for (int i = 0; i < S; i++) {
                if (total_supply >= total_demand) break;
                int node_idx = suppliers[i].idx;
                if (production_rate[node_idx] <= 0.0f) continue;

                int tranche_cap = static_cast<int>(
                    production_rate[node_idx] * static_cast<float>(saturation_window));
                if (tranche_cap <= 0) continue;

                float tranche_cost = static_cast<float>(k) *
                    static_cast<float>(saturation_window) * saturation_cost_scale;

                add_mcf_edge(SRC, 1 + i, tranche_cap, tranche_cost);
                total_supply += tranche_cap;
            }
        }
    }
    // Supply → demand edges (uncapacitated).
    // When demand_value is provided, cost = dist / value so high-value
    // targets are cheaper to serve (prioritized by SSP).
    bool has_values = !demand_value.empty();
    for (int i = 0; i < S; i++) {
        for (int j = 0; j < D; j++) {
            float c = sp_.dist[suppliers[i].idx * N_ + demanders[j].idx];
            if (c < std::numeric_limits<float>::infinity()) {
                if (has_values) {
                    float v = demand_value[demanders[j].idx];
                    if (v > 0.0f) c /= v;
                }
                add_mcf_edge(1 + i, S + 1 + j, std::numeric_limits<int>::max() / 2, c);
            }
        }
    }
    // Demand → sink edges
    for (int j = 0; j < D; j++) {
        add_mcf_edge(S + 1 + j, SINK, demanders[j].remaining, 0.0f);
    }

    // SSP with Johnson potentials
    std::vector<float> pot(V, 0.0f);

    using PQEntry = std::pair<float, int>;
    std::vector<float> dist_v(V);
    std::vector<int> prev_node(V);
    std::vector<int> prev_edge(V);

    for (;;) {
        // Dijkstra with reduced costs
        dist_v.assign(V, std::numeric_limits<float>::infinity());
        prev_node.assign(V, -1);
        prev_edge.assign(V, -1);
        dist_v[SRC] = 0.0f;

        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
        pq.push({0.0f, SRC});

        while (!pq.empty()) {
            auto [du, u] = pq.top();
            pq.pop();
            if (du > dist_v[u]) continue;

            for (int ei = 0; ei < static_cast<int>(adj[u].size()); ei++) {
                const auto& e = adj[u][ei];
                if (e.cap <= 0) continue;
                float reduced = e.cost + pot[u] - pot[e.to];
                if (reduced < 0.0f) reduced = 0.0f;  // clamp FP noise
                float nd = dist_v[u] + reduced;
                if (nd < dist_v[e.to]) {
                    dist_v[e.to] = nd;
                    prev_node[e.to] = u;
                    prev_edge[e.to] = ei;
                    pq.push({nd, e.to});
                }
            }
        }

        if (dist_v[SINK] >= std::numeric_limits<float>::infinity()) break;

        // Update Johnson potentials
        for (int v = 0; v < V; v++) {
            if (dist_v[v] < std::numeric_limits<float>::infinity()) {
                pot[v] += dist_v[v];
            }
        }

        // Find bottleneck capacity along the path
        int bottleneck = std::numeric_limits<int>::max();
        for (int v = SINK; v != SRC; v = prev_node[v]) {
            bottleneck = std::min(bottleneck, adj[prev_node[v]][prev_edge[v]].cap);
        }

        // Push flow along the path (update residual capacities)
        for (int v = SINK; v != SRC; v = prev_node[v]) {
            int u = prev_node[v];
            int ei = prev_edge[v];
            adj[u][ei].cap -= bottleneck;
            adj[v][adj[u][ei].rev].cap += bottleneck;
        }
    }

    // Extract final supply→demand flows from residual capacities.
    // The incremental tracking was buggy: SSP rerouting via reverse edges
    // cancels previous assignments, but the incremental tracker only added.
    const int UNCAP = std::numeric_limits<int>::max() / 2;
    std::vector<int> flow_on_sd(S * D, 0);
    for (int i = 0; i < S; i++) {
        for (const auto& e : adj[1 + i]) {
            if (e.to >= S + 1 && e.to <= S + D) {
                int j = e.to - (S + 1);
                int flow = UNCAP - e.cap;
                if (flow > 0) {
                    flow_on_sd[i * D + j] = flow;
                }
            }
        }
    }

    // ========================================================================
    // Convert flow assignments to per-edge flows on the game graph.
    // For each supply→demand flow, trace shortest path and accumulate.
    // ========================================================================

    std::vector<std::vector<std::pair<int, int>>> edge_flow(N_);

    auto add_edge_flow = [&](int u, int v, int amount) {
        for (auto& [nbr, f] : edge_flow[u]) {
            if (nbr == v) { f += amount; return; }
        }
        edge_flow[u].push_back({v, amount});
    };

    // Per-node flow toward each demand (for Voronoi)
    std::vector<std::vector<std::pair<int, int>>> node_demand_flow(N_);

    auto add_node_demand = [&](int node, int demand_node, int amount) {
        for (auto& [d, f] : node_demand_flow[node]) {
            if (d == demand_node) { f += amount; return; }
        }
        node_demand_flow[node].push_back({demand_node, amount});
    };

    for (int i = 0; i < S; i++) {
        for (int j = 0; j < D; j++) {
            int f = flow_on_sd[i * D + j];
            if (f <= 0) continue;

            int src_node = suppliers[i].idx;
            int dst_node = demanders[j].idx;

            // Trace shortest path src → dst:
            // - Full path for Voronoi (node_demand_flow)
            // - First hop only for edge_flow (avoid phantom forwarding)
            int cur = dst_node;
            while (cur != src_node) {
                int prev = sp_.pred[src_node * N_ + cur];
                if (prev < 0) break;
                add_node_demand(prev, dst_node, f);
                add_node_demand(cur, dst_node, f);
                if (prev == src_node) {
                    add_edge_flow(prev, cur, f);
                }
                cur = prev;
            }
        }
    }

    // Cancel anti-parallel flows: if edge (u,v) has flow in both directions,
    // net them out. The bipartite SSP can produce crossing paths whose graph
    // decompositions share edges in opposite directions.
    for (int u = 0; u < N_; u++) {
        for (auto& [v, f_uv] : edge_flow[u]) {
            if (f_uv <= 0) continue;
            for (auto& [w, f_vu] : edge_flow[v]) {
                if (w == u && f_vu > 0) {
                    int cancel = std::min(f_uv, f_vu);
                    f_uv -= cancel;
                    f_vu -= cancel;
                }
            }
        }
    }

    // Build Voronoi: each node → demand node with most flow through it
    voronoi_.assign(N_, -1);
    for (int i = 0; i < N_; i++) {
        int best_flow = 0;
        for (const auto& [d, f] : node_demand_flow[i]) {
            if (f > best_flow) {
                best_flow = f;
                voronoi_[i] = d;
            }
        }
    }

    // Convert edge flows to TroopCommands, capped by available troops
    std::vector<TroopCommand> commands;
    for (int u = 0; u < N_; u++) {
        if (edge_flow[u].empty()) continue;
        if (nodes[u].owner != player_id) continue;
        if (masked[u]) continue;

        int available = nodes[u].troops[player_id] - 1;  // keep at least 1
        if (available <= 0) continue;

        // Total desired outflow from this node
        int total_desired = 0;
        for (const auto& [v, f] : edge_flow[u]) total_desired += f;
        if (total_desired <= 0) continue;

        // Budget = min(available, total_desired)
        int budget = std::min(available, total_desired);

        if (budget == total_desired) {
            // Can send everything requested
            for (const auto& [v, f] : edge_flow[u]) {
                if (f > 0) commands.push_back({u, v, f});
            }
        } else {
            // Proportional allocation with largest-remainder rounding
            struct Alloc { int dest; float share; int floor_val; };
            std::vector<Alloc> allocs;
            int total_floor = 0;
            float budget_f = static_cast<float>(budget);
            float total_f = static_cast<float>(total_desired);

            for (const auto& [v, f] : edge_flow[u]) {
                float share = (static_cast<float>(f) / total_f) * budget_f;
                int fl = static_cast<int>(share);
                allocs.push_back({v, share - static_cast<float>(fl), fl});
                total_floor += fl;
            }

            int remainder = budget - total_floor;
            if (remainder > 0) {
                std::vector<int> idx(allocs.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                    return allocs[a].share > allocs[b].share;
                });
                for (int r = 0; r < remainder && r < static_cast<int>(idx.size()); r++) {
                    allocs[idx[r]].floor_val++;
                }
            }

            for (const auto& a : allocs) {
                if (a.floor_val > 0) commands.push_back({u, a.dest, a.floor_val});
            }
        }
    }

    return commands;
}

// ============================================================================
// Factory
// ============================================================================

TransportSolver make_ot_solver(const Graph& graph, const std::vector<int>& target_troops) {
    auto solver = std::make_shared<OTSolver>(graph, target_troops);
    return [solver](const Graph&, const std::vector<NodeData>& nodes,
                    const std::vector<float>&, int player_id,
                    const std::vector<bool>& masked) {
        return solver->solve(nodes, player_id, masked);
    };
}
