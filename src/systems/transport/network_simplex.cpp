#include "systems/transport/network_simplex.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <vector>

static constexpr int BIG_CAP = 1'000'000;
static constexpr float BIG_M = 1e7f;

// ============================================================================
// Constructor: preallocate ALL arcs (game + dynamic + artificial)
// ============================================================================

NetworkSimplex::NetworkSimplex(const Graph& game_graph)
    : N_(game_graph.num_nodes())
    , num_nodes_(game_graph.num_nodes() + 2)
    , SRC_(game_graph.num_nodes())
    , SINK_(game_graph.num_nodes() + 1)
    , graph_(&game_graph)
    , voronoi_(game_graph.num_nodes(), -1) {

    build_static_arcs();

    src_base_  = static_end_;
    sink_base_ = static_end_ + N_;
    art_base_  = static_end_ + 2 * N_;

    // Preallocate SRC→i arcs (inactive: cap=0)
    for (int i = 0; i < N_; i++)
        arcs_.push_back({SRC_, i, 0, 0.0f, 0});

    // Preallocate i→SINK arcs (inactive: cap=0)
    for (int i = 0; i < N_; i++)
        arcs_.push_back({i, SINK_, 0, 0.0f, 0});

    // Preallocate artificial arcs (one per non-root node)
    // Order: game nodes 0..N-1, then SINK
    for (int i = 0; i < num_nodes_; i++) {
        if (i == SRC_) continue;
        arcs_.push_back({SRC_, i, BIG_CAP, BIG_M, 0});
    }

    // Pre-size basis arrays
    parent_.resize(num_nodes_);
    parent_arc_.resize(num_nodes_);
    potential_.resize(num_nodes_);
    depth_.resize(num_nodes_);
    thread_.resize(num_nodes_);
}

void NetworkSimplex::build_static_arcs() {
    arcs_.clear();
    for (const auto& e : graph_->edges) {
        arcs_.push_back({e.a_idx, e.b_idx, BIG_CAP, e.length, 0});
        arcs_.push_back({e.b_idx, e.a_idx, BIG_CAP, e.length, 0});
    }
    static_end_ = static_cast<int>(arcs_.size());
}

// ============================================================================
// Update dynamic arc caps/costs in place (no realloc, no rebuild)
// ============================================================================

void NetworkSimplex::update_dynamic_arcs(
    const std::vector<int>& supply, const std::vector<int>& demand,
    const std::vector<float>& demand_val, float value_alpha,
    const std::vector<float>& production_rate,
    const std::vector<bool>& masked, int player_id,
    const std::vector<NodeData>& nodes) {

    bool has_values = !demand_val.empty();
    bool has_production = !production_rate.empty();

    producer_arc_indices_.clear();
    producer_node_indices_.clear();

    for (int i = 0; i < N_; i++) {
        auto& src_arc = arcs_[src_base_ + i];
        auto& sink_arc = arcs_[sink_base_ + i];

        // Reset both arcs to inactive
        src_arc.cap = 0;
        src_arc.cost = 0.0f;
        src_arc.flow = 0;
        sink_arc.cap = 0;
        sink_arc.cost = 0.0f;
        sink_arc.flow = 0;

        // SRC→i: supply (existing troops) or producer (future production)
        if (supply[i] > 0) {
            src_arc.cap = supply[i];
        } else if (has_production && !masked[i] && production_rate[i] > 0.0f
                   && demand[i] == 0 && nodes[i].owner == player_id) {
            src_arc.cap = BIG_CAP;
            // cost starts at 0, FW updates it
            producer_arc_indices_.push_back(src_base_ + i);
            producer_node_indices_.push_back(i);
        }

        // i→SINK: demand
        if (demand[i] > 0) {
            sink_arc.cap = demand[i];
            if (has_values && value_alpha > 0.0f && demand_val[i] > 0.0f) {
                sink_arc.cost = -value_alpha * demand_val[i];
            }
        }
    }
}

// ============================================================================
// Artificial basis initialization (Big-M method)
//
// Uses preallocated artificial arcs at art_base_. Sets flows and builds
// the initial spanning tree: SRC as root, all others as children.
// ============================================================================

void NetworkSimplex::initialize_artificial_basis() {
    // Reset flows on all non-artificial arcs
    for (int a = 0; a < art_base_; a++) {
        arcs_[a].flow = 0;
    }

    // Compute total flow F = min(total_supply_capacity, total_demand)
    int total_src_cap = 0;
    int total_sink_cap = 0;
    for (int i = 0; i < N_; i++) {
        total_src_cap += std::min(arcs_[src_base_ + i].cap, 100000);
        total_sink_cap += arcs_[sink_base_ + i].cap;
    }
    int F = std::min(total_src_cap, total_sink_cap);

    // Set up spanning tree: SRC as root, all others as children via artificial arcs
    int root = SRC_;
    parent_[root] = -1;
    depth_[root] = 0;
    potential_[root] = 0.0f;

    int art_idx = art_base_;
    int prev = root;
    for (int i = 0; i < num_nodes_; i++) {
        if (i == root) continue;

        // Set artificial arc flow
        arcs_[art_idx].flow = (i == SINK_) ? F : 0;

        parent_[i] = root;
        parent_arc_[i] = art_idx;
        depth_[i] = 1;
        potential_[i] = -BIG_M;

        thread_[prev] = i;
        prev = i;
        art_idx++;
    }
    thread_[prev] = root;

    has_basis_ = true;
}

// ============================================================================
// Find entering arc: most negative reduced cost (Dantzig's rule)
// ============================================================================

int NetworkSimplex::find_entering_arc() {
    float best_violation = -1e-6f;
    int best_arc = -1;

    int num_arcs = static_cast<int>(arcs_.size());
    for (int a = 0; a < num_arcs; a++) {
        const auto& arc = arcs_[a];
        if (arc.flow < arc.cap) {
            float rc = arc.cost - potential_[arc.from] + potential_[arc.to];
            if (rc < best_violation) {
                best_violation = rc;
                best_arc = a;
            }
        }
    }

    return best_arc;
}

// ============================================================================
// Find LCA using depth array
// ============================================================================

int NetworkSimplex::find_lca(int u, int v) {
    while (u != v) {
        if (depth_[u] > depth_[v])
            u = parent_[u];
        else
            v = parent_[v];
    }
    return u;
}

// ============================================================================
// Pivot
// ============================================================================

void NetworkSimplex::pivot(int entering) {
    auto& ent = arcs_[entering];
    int u = ent.from;
    int v = ent.to;
    int lca = find_lca(u, v);

    int delta = ent.cap - ent.flow;
    int leaving_arc = entering;
    int leaving_node = -1;

    // Walk u to LCA
    {
        int cur = u;
        while (cur != lca) {
            int pa = parent_arc_[cur];
            const auto& arc = arcs_[pa];
            if (arc.from == cur) {
                if (arc.flow < delta) {
                    delta = arc.flow;
                    leaving_arc = pa;
                    leaving_node = cur;
                }
            } else {
                int slack = arc.cap - arc.flow;
                if (slack < delta) {
                    delta = slack;
                    leaving_arc = pa;
                    leaving_node = cur;
                }
            }
            cur = parent_[cur];
        }
    }

    // Walk v to LCA
    {
        int cur = v;
        while (cur != lca) {
            int pa = parent_arc_[cur];
            const auto& arc = arcs_[pa];
            if (arc.to == cur) {
                if (arc.flow < delta) {
                    delta = arc.flow;
                    leaving_arc = pa;
                    leaving_node = cur;
                }
            } else {
                int slack = arc.cap - arc.flow;
                if (slack < delta) {
                    delta = slack;
                    leaving_arc = pa;
                    leaving_node = cur;
                }
            }
            cur = parent_[cur];
        }
    }

    if (delta <= 0) {
        if (leaving_arc != entering && leaving_node >= 0) {
            // degenerate pivot: swap tree arcs
        } else {
            return;
        }
    }

    // Push flow
    ent.flow += delta;
    {
        int cur = u;
        while (cur != lca) {
            int pa = parent_arc_[cur];
            auto& arc = arcs_[pa];
            if (arc.from == cur) arc.flow -= delta;
            else arc.flow += delta;
            cur = parent_[cur];
        }
    }
    {
        int cur = v;
        while (cur != lca) {
            int pa = parent_arc_[cur];
            auto& arc = arcs_[pa];
            if (arc.to == cur) arc.flow -= delta;
            else arc.flow += delta;
            cur = parent_[cur];
        }
    }

    if (leaving_arc == entering) return;

    // Update spanning tree
    auto in_subtree = [&](int node, int subtree_root) -> bool {
        int cur = node;
        while (cur != -1 && depth_[cur] >= depth_[subtree_root]) {
            if (cur == subtree_root) return true;
            cur = parent_[cur];
        }
        return false;
    };

    bool u_in_subtree = in_subtree(u, leaving_node);
    int enter_end_in_subtree = u_in_subtree ? u : v;
    int enter_end_out = u_in_subtree ? v : u;

    // Reverse parent pointers
    std::vector<int> path;
    {
        int cur = enter_end_in_subtree;
        while (cur != leaving_node) {
            path.push_back(cur);
            cur = parent_[cur];
        }
        path.push_back(leaving_node);
    }

    for (int k = static_cast<int>(path.size()) - 1; k >= 1; k--) {
        parent_[path[k]] = path[k - 1];
        parent_arc_[path[k]] = parent_arc_[path[k - 1]];
    }

    parent_[enter_end_in_subtree] = enter_end_out;
    parent_arc_[enter_end_in_subtree] = entering;

    // Rebuild depth and potentials for modified subtree
    {
        std::queue<int> q;
        depth_[enter_end_in_subtree] = depth_[enter_end_out] + 1;
        {
            const auto& arc = arcs_[entering];
            if (arc.from == enter_end_out)
                potential_[enter_end_in_subtree] = potential_[enter_end_out] - arc.cost;
            else
                potential_[enter_end_in_subtree] = potential_[enter_end_out] + arc.cost;
        }
        q.push(enter_end_in_subtree);

        while (!q.empty()) {
            int node = q.front();
            q.pop();
            for (int j = 0; j < num_nodes_; j++) {
                if (parent_[j] == node && j != enter_end_in_subtree) {
                    depth_[j] = depth_[node] + 1;
                    const auto& arc = arcs_[parent_arc_[j]];
                    if (arc.from == node)
                        potential_[j] = potential_[node] - arc.cost;
                    else
                        potential_[j] = potential_[node] + arc.cost;
                    q.push(j);
                }
            }
        }
    }

    // Rebuild thread order via DFS from root
    {
        std::vector<std::vector<int>> children(num_nodes_);
        for (int i = 0; i < num_nodes_; i++) {
            if (parent_[i] >= 0)
                children[parent_[i]].push_back(i);
        }
        std::vector<int> order;
        order.reserve(num_nodes_);
        std::vector<int> stk = {SRC_};
        while (!stk.empty()) {
            int nd = stk.back();
            stk.pop_back();
            order.push_back(nd);
            for (int c = static_cast<int>(children[nd].size()) - 1; c >= 0; c--)
                stk.push_back(children[nd][c]);
        }
        for (int i = 0; i + 1 < static_cast<int>(order.size()); i++)
            thread_[order[i]] = order[i + 1];
        if (!order.empty())
            thread_[order.back()] = order[0];
    }
}

// ============================================================================
// Run simplex until optimal
// ============================================================================

void NetworkSimplex::run_simplex() {
    for (;;) {
        int entering = find_entering_arc();
        if (entering < 0) break;
        pivot(entering);
        last_pivot_count_++;
        if (last_pivot_count_ > 100000) break;
    }
}

// ============================================================================
// Recompute potentials from spanning tree (O(N) via thread order)
// ============================================================================

void NetworkSimplex::recompute_potentials() {
    potential_[SRC_] = 0.0f;
    int cur = thread_[SRC_];
    while (cur != SRC_) {
        int par = parent_[cur];
        const auto& arc = arcs_[parent_arc_[cur]];
        if (arc.from == par)
            potential_[cur] = potential_[par] - arc.cost;
        else
            potential_[cur] = potential_[par] + arc.cost;
        cur = thread_[cur];
    }
}

// ============================================================================
// Voronoi: BFS backward from demand nodes along flow-carrying arcs
// ============================================================================

void NetworkSimplex::extract_voronoi(const std::vector<int>& demand_nodes) {
    voronoi_.assign(N_, -1);

    std::vector<std::vector<std::pair<int, int>>> node_demand_flow(N_);

    for (int d : demand_nodes) {
        std::vector<bool> visited(N_, false);
        std::queue<int> q;
        visited[d] = true;
        node_demand_flow[d].push_back({d, 0});
        q.push(d);

        while (!q.empty()) {
            int vv = q.front();
            q.pop();

            for (int a = 0; a < static_end_; a++) {
                const auto& arc = arcs_[a];
                if (arc.to == vv && arc.flow > 0 && !visited[arc.from]) {
                    visited[arc.from] = true;
                    bool found = false;
                    for (auto& [dd, ff] : node_demand_flow[arc.from]) {
                        if (dd == d) { ff += arc.flow; found = true; break; }
                    }
                    if (!found) {
                        node_demand_flow[arc.from].push_back({d, arc.flow});
                    }
                    q.push(arc.from);
                }
            }
        }
    }

    for (int i = 0; i < N_; i++) {
        int best_flow = 0;
        for (const auto& [d, f] : node_demand_flow[i]) {
            if (f > best_flow) {
                best_flow = f;
                voronoi_[i] = d;
            }
        }
    }
}

// ============================================================================
// Extract TroopCommands from flow on game arcs
// ============================================================================

std::vector<TroopCommand> NetworkSimplex::extract_commands(
    const std::vector<NodeData>& nodes, int player_id,
    const std::vector<bool>& masked) {

    // Cancel anti-parallel flows
    for (int a = 0; a + 1 < static_end_; a += 2) {
        auto& fwd = arcs_[a];
        auto& rev = arcs_[a + 1];
        if (fwd.flow > 0 && rev.flow > 0) {
            int cancel = std::min(fwd.flow, rev.flow);
            fwd.flow -= cancel;
            rev.flow -= cancel;
        }
    }

    struct OutFlow { int neighbor; int flow; };
    std::vector<std::vector<OutFlow>> outflows(N_);
    for (int a = 0; a < static_end_; a++) {
        const auto& arc = arcs_[a];
        if (arc.flow > 0 && arc.from < N_)
            outflows[arc.from].push_back({arc.to, arc.flow});
    }

    std::vector<TroopCommand> commands;
    for (int u = 0; u < N_; u++) {
        if (outflows[u].empty()) continue;
        if (nodes[u].owner != player_id) continue;
        if (masked[u]) continue;

        int available = nodes[u].troops[player_id] - 1;
        if (available <= 0) continue;

        int total_desired = 0;
        for (const auto& [nb, f] : outflows[u]) total_desired += f;
        if (total_desired <= 0) continue;

        int budget = std::min(available, total_desired);

        if (budget >= total_desired) {
            for (const auto& [nb, f] : outflows[u]) {
                if (f > 0) commands.push_back({u, nb, f});
            }
        } else {
            struct Alloc { int dest; float share; int floor_val; };
            std::vector<Alloc> allocs;
            int total_floor = 0;
            float budget_f = static_cast<float>(budget);
            float total_f = static_cast<float>(total_desired);

            for (const auto& [nb, f] : outflows[u]) {
                float s = (static_cast<float>(f) / total_f) * budget_f;
                int fl = static_cast<int>(s);
                allocs.push_back({nb, s - static_cast<float>(fl), fl});
                total_floor += fl;
            }

            int remainder = budget - total_floor;
            if (remainder > 0) {
                std::vector<int> idx(allocs.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](int aa, int bb) {
                    return allocs[aa].share > allocs[bb].share;
                });
                for (int r = 0; r < remainder && r < static_cast<int>(idx.size()); r++)
                    allocs[idx[r]].floor_val++;
            }

            for (const auto& al : allocs) {
                if (al.floor_val > 0) commands.push_back({u, al.dest, al.floor_val});
            }
        }
    }

    return commands;
}

// ============================================================================
// Main solve with Frank-Wolfe decomposition
// ============================================================================

std::vector<TroopCommand> NetworkSimplex::solve(
    const std::vector<NodeData>& nodes,
    int player_id,
    const std::vector<bool>& masked,
    const std::vector<int>& targets,
    const std::vector<float>& demand_value,
    const std::vector<float>& effective_troops,
    const std::vector<float>& production_rate,
    float saturation_alpha,
    float value_alpha,
    int fw_iterations) {

    bool has_effective = !effective_troops.empty();

    // Compute supply and demand
    std::vector<int> supply(N_, 0);
    std::vector<int> demand(N_, 0);
    for (int i = 0; i < N_; i++) {
        int current_node = nodes[i].troops[player_id];
        int current_effective = has_effective
            ? static_cast<int>(effective_troops[i])
            : current_node;
        int target = targets[i];
        if (current_node > target) {
            supply[i] = current_node - target;
        } else if (target > current_effective) {
            demand[i] = target - current_effective;
        }
    }

    for (int i = 0; i < N_; i++) {
        if (masked[i]) supply[i] = 0;
    }

    int total_supply = 0, total_demand = 0;
    for (int i = 0; i < N_; i++) {
        total_supply += supply[i];
        total_demand += demand[i];
    }

    if (total_demand == 0 || total_supply == 0) {
        voronoi_.assign(N_, -1);
        last_pivot_count_ = 0;
        last_was_warm_ = false;
        last_fw_iterations_ = 0;
        return {};
    }

    // Update dynamic arc caps/costs in place
    update_dynamic_arcs(supply, demand, demand_value, value_alpha,
                        production_rate, masked, player_id, nodes);

    // Cold start (artificial basis on preallocated arcs)
    initialize_artificial_basis();
    last_was_warm_ = false;
    last_pivot_count_ = 0;

    // Determine if we need FW iterations
    bool use_fw = saturation_alpha > 0.0f && !producer_arc_indices_.empty()
                  && fw_iterations > 1;
    int K = use_fw ? fw_iterations : 1;

    if (!use_fw) {
        run_simplex();
        last_fw_iterations_ = 1;
    } else {
        // Frank-Wolfe with warm-started LP re-solves between iterations
        int num_arcs = static_cast<int>(arcs_.size());

        // Iteration 0: cold start with zero producer costs
        run_simplex();

        std::vector<float> x_k(num_arcs);
        for (int a = 0; a < num_arcs; a++)
            x_k[a] = static_cast<float>(arcs_[a].flow);

        for (int k = 1; k < K; k++) {
            // Update producer costs: gradient of alpha*f^2 is 2*alpha*f
            for (int p = 0; p < static_cast<int>(producer_arc_indices_.size()); p++) {
                int ai = producer_arc_indices_[p];
                arcs_[ai].cost = 2.0f * saturation_alpha * x_k[ai];
            }

            // Warm start: recompute potentials, re-optimize
            recompute_potentials();
            run_simplex();

            // Blend: x_{k+1} = (1-gamma)*x_k + gamma*s_k
            float gamma = 2.0f / static_cast<float>(k + 2);
            for (int a = 0; a < num_arcs; a++) {
                x_k[a] = (1.0f - gamma) * x_k[a]
                        + gamma * static_cast<float>(arcs_[a].flow);
            }
        }

        // Set final blended flows
        for (int a = 0; a < num_arcs; a++) {
            int rounded = static_cast<int>(std::round(x_k[a]));
            arcs_[a].flow = std::clamp(rounded, 0, arcs_[a].cap);
        }

        last_fw_iterations_ = K;
    }

    // Voronoi
    std::vector<int> demand_nodes;
    for (int i = 0; i < N_; i++) {
        if (demand[i] > 0) demand_nodes.push_back(i);
    }
    extract_voronoi(demand_nodes);

    return extract_commands(nodes, player_id, masked);
}
