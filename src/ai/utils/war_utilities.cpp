#include "ai/utils/war_utilities.hpp"
#include "engine/game.hpp"

#include <algorithm>
#include <cstdint>

// --- Target evaluation ---

float frontier_target_cost(const Game& game, int node_idx, int player_id) {
    const auto& nd = game.node_data()[node_idx];
    const auto& config = game.config();

    float total_enemy_troops = 0.0f;
    for (int p = 0; p < game.n_players(); p++) {
        if (p == player_id) continue;
        total_enemy_troops += static_cast<float>(nd.troops[p]);
    }

    if (nd.state == NodeState::FORT && nd.owner >= 0) {
        total_enemy_troops *= config.fort_defense_mult;
    }

    return total_enemy_troops;
}

float frontier_target_value(const Game& game, int node_idx, int player_id,
                            const ModelConfig& cfg) {
    const auto& nd = game.node_data()[node_idx];
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();

    float value = 1.0f;

    if (nd.state == NodeState::POWERPLANT && nd.owner >= 0 && nd.owner != player_id) {
        int powered_production = 0;
        for (int nbr : graph.neighbors(node_idx)) {
            const auto& nbd = nodes_data[nbr];
            if (nbd.owner == nd.owner) {
                if (nbd.state == NodeState::FACTORY || nbd.state == NodeState::CAPITAL) {
                    powered_production++;
                }
            }
        }
        value += cfg.war_powerplant_value * static_cast<float>(powered_production);
    }

    if (nd.state == NodeState::FACTORY && nd.owner >= 0 && nd.owner != player_id) {
        value += cfg.war_factory_value;
        for (int nbr : graph.neighbors(node_idx)) {
            const auto& nbd = nodes_data[nbr];
            if (nbd.owner == nd.owner && nbd.state == NodeState::POWERPLANT) {
                value += cfg.war_powerplant_value;
            }
        }
    }

    if (nd.state == NodeState::CAPITAL) {
        value += cfg.war_capital_value;
    }

    if (nd.state == NodeState::ARTILLERY && nd.owner >= 0 && nd.owner != player_id) {
        value += cfg.war_artillery_value;
    }

    return value;
}

// --- Frontier extraction ---

std::vector<FrontierTarget> extract_frontier(const Game& game, int player_id,
                                              float& budget_out,
                                              const ModelConfig& cfg) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    int n = graph.num_nodes();

    std::unordered_set<int> our_nodes;
    for (int i = 0; i < n; i++) {
        if (nodes_data[i].owner == player_id) {
            our_nodes.insert(i);
        }
    }

    std::unordered_set<int> opposing_frontier;
    for (int node : our_nodes) {
        for (int nbr : graph.neighbors(node)) {
            if (our_nodes.find(nbr) == our_nodes.end()) {
                opposing_frontier.insert(nbr);
            }
        }
    }

    std::unordered_set<int> our_frontier;
    for (int target : opposing_frontier) {
        for (int nbr : graph.neighbors(target)) {
            if (our_nodes.find(nbr) != our_nodes.end()) {
                our_frontier.insert(nbr);
            }
        }
    }

    budget_out = 0.0f;
    for (int node : our_frontier) {
        budget_out += static_cast<float>(nodes_data[node].troops[player_id]);
    }

    std::vector<FrontierTarget> targets;
    targets.reserve(opposing_frontier.size());

    for (int target : opposing_frontier) {
        FrontierTarget ft;
        ft.node_idx = target;
        ft.cost = frontier_target_cost(game, target, player_id);
        ft.value = frontier_target_value(game, target, player_id, cfg);
        ft.ratio = (ft.cost > 0.0f) ? ft.value / ft.cost : 1e6f;
        targets.push_back(ft);
    }

    std::sort(targets.begin(), targets.end(),
              [](const FrontierTarget& a, const FrontierTarget& b) {
                  return a.ratio > b.ratio;
              });

    return targets;
}

// --- Knapsack solvers ---

std::vector<int> knapsack_greedy(const std::vector<FrontierTarget>& targets,
                                  float budget) {
    std::vector<int> selected;
    float remaining = budget;

    for (int i = 0; i < static_cast<int>(targets.size()); i++) {
        if (remaining < targets[i].cost) continue;
        remaining -= targets[i].cost;
        selected.push_back(i);
    }

    return selected;
}

std::vector<int> knapsack_optimal(const std::vector<FrontierTarget>& targets,
                                   float budget) {
    int n = static_cast<int>(targets.size());
    if (n == 0) return {};
    if (n > 20) {
        return knapsack_greedy(targets, budget);
    }

    float best_value = -1.0f;
    uint32_t best_mask = 0;

    uint32_t limit = static_cast<uint32_t>(1) << n;
    for (uint32_t mask = 0; mask < limit; mask++) {
        float total_cost = 0.0f;
        float total_value = 0.0f;

        for (int i = 0; i < n; i++) {
            if (mask & (static_cast<uint32_t>(1) << i)) {
                total_cost += targets[i].cost;
                total_value += targets[i].value;
            }
        }

        if (total_cost <= budget && total_value > best_value) {
            best_value = total_value;
            best_mask = mask;
        }
    }

    std::vector<int> selected;
    for (int i = 0; i < n; i++) {
        if (best_mask & (static_cast<uint32_t>(1) << i)) {
            selected.push_back(i);
        }
    }

    return selected;
}

float selection_value(const std::vector<FrontierTarget>& targets,
                      const std::vector<int>& selected) {
    float total = 0.0f;
    for (int i : selected) {
        total += targets[i].value;
    }
    return total;
}

float selection_cost(const std::vector<FrontierTarget>& targets,
                     const std::vector<int>& selected) {
    float total = 0.0f;
    for (int i : selected) {
        total += targets[i].cost;
    }
    return total;
}

// --- V2 per-node budget solver ---

float v2_target_priority(float value, int degree, int K) {
    float denom = static_cast<float>(degree) / static_cast<float>(K) + 1.0f;
    return value / denom;
}

std::vector<TroopCommand> v2_solve_attacks(
    const Game& game, int player_id,
    const std::vector<V2Target>& targets,
    std::vector<int>& available) {

    std::vector<TroopCommand> commands;

    const auto& edge_lanes = game.edge_lanes();
    const auto& graph = game.graph();

    for (const auto& target : targets) {
        int supply = 0;
        for (int nbr : target.our_neighbors) {
            supply += available[nbr];
        }

        int in_flight = 0;
        for (int nbr : target.our_neighbors) {
            int eidx = graph.edge_between(nbr, target.node_idx);
            if (eidx < 0) continue;
            const auto& el = edge_lanes[eidx];
            int lane_idx = (nbr == el.node_a) ? 0 : 1;
            for (const auto& g : el.lanes[lane_idx].groups) {
                if (g.owner == player_id && !g.retreating) {
                    in_flight += g.count;
                }
            }
        }

        int cost_int = static_cast<int>(target.cost);
        if (supply + in_flight <= cost_int) continue;

        int still_needed = cost_int + 1 - in_flight;
        if (still_needed <= 0) continue;

        std::vector<int> sorted_neighbors = target.our_neighbors;
        std::sort(sorted_neighbors.begin(), sorted_neighbors.end(),
                  [&graph](int a, int b) {
                      return graph.degree(a)
                           < graph.degree(b);
                  });

        int remaining_to_send = still_needed;
        for (int nbr : sorted_neighbors) {
            if (remaining_to_send <= 0) break;
            int send = std::min(available[nbr], remaining_to_send);
            if (send > 0) {
                commands.push_back({nbr, target.node_idx, send});
                available[nbr] -= send;
                remaining_to_send -= send;
            }
        }
    }

    return commands;
}

// --- Shared war context ---

WarContext build_war_context(const Game& game, int player_id,
                             const ModelConfig& cfg) {
    const auto& graph = game.graph();
    const auto& nodes_data = game.node_data();
    int n = graph.num_nodes();
    int K = game.config().max_neighbors;

    WarContext ctx;
    ctx.available.assign(n, 0);

    for (int i = 0; i < n; i++) {
        if (nodes_data[i].owner == player_id) {
            ctx.our_nodes.insert(i);
            int troops = nodes_data[i].troops[player_id];
            ctx.available[i] = std::max(0, troops - 1);
        }
    }

    std::unordered_set<int> opposing_frontier;
    for (int node : ctx.our_nodes) {
        for (int nbr : graph.neighbors(node)) {
            if (ctx.our_nodes.find(nbr) == ctx.our_nodes.end()) {
                opposing_frontier.insert(nbr);
            }
        }
    }

    ctx.targets.reserve(opposing_frontier.size());

    for (int target_idx : opposing_frontier) {
        int target_owner = nodes_data[target_idx].owner;
        if (target_owner < 0 || target_owner == player_id) continue;
        if (target_owner >= game.n_real_players()) continue;
        if (!game.is_alive(target_owner)) continue;

        V2Target t;
        t.node_idx = target_idx;
        t.cost = frontier_target_cost(game, target_idx, player_id);
        t.value = frontier_target_value(game, target_idx, player_id, cfg);

        for (int nbr : graph.neighbors(target_idx)) {
            if (ctx.our_nodes.find(nbr) != ctx.our_nodes.end()) {
                t.our_neighbors.push_back(nbr);
            }
        }

        int degree = graph.degree(target_idx);
        t.priority = v2_target_priority(t.value, degree, K);
        ctx.targets.push_back(std::move(t));
    }

    std::sort(ctx.targets.begin(), ctx.targets.end(),
              [](const V2Target& a, const V2Target& b) {
                  return a.priority > b.priority;
              });

    return ctx;
}
