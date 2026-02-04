#include "game.hpp"
#include "combat.hpp"
#include "production.hpp"
#include "routing.hpp"

#include <algorithm>

Game::Game(const GameConfig& config, const std::vector<int>& capitals, uint64_t seed)
    : config_(config), n_real_players_(static_cast<int>(capitals.size())) {

    graph_ = Graph::generate_poisson(config_, seed);
    init_state(capitals);
}

Game::Game(const GameConfig& config, Graph graph, const std::vector<int>& capitals)
    : config_(config), n_real_players_(static_cast<int>(capitals.size())),
      graph_(std::move(graph)) {

    init_state(capitals);
}

void Game::init_state(const std::vector<int>& capitals) {
    n_players_ = n_real_players_;
    // Add a neutral player if default troops are configured
    bool has_neutral = config_.init_default_troops > 0;
    if (has_neutral) n_players_ = n_real_players_ + 1;
    int neutral_id = n_real_players_; // last player slot

    // Initialize node data
    node_data_.resize(graph_.num_nodes());
    for (NodeData& nd : node_data_) {
        nd.troops.assign(n_players_, 0);
        nd.accumulated_damage.assign(n_players_, 0.0f);
    }

    // Place capitals
    alive_.assign(n_players_, true);
    for (int p = 0; p < n_real_players_; p++) {
        int node = capitals[p];
        node_data_[node].state = NodeState::CAPITAL;
        node_data_[node].owner = p;
        node_data_[node].troops[p] = config_.init_troop_count;
    }

    // Place neutral defenders on non-capital nodes
    if (has_neutral) {
        for (int i = 0; i < graph_.num_nodes(); i++) {
            if (node_data_[i].state == NodeState::CAPITAL) continue;
            node_data_[i].owner = neutral_id;
            node_data_[i].troops[neutral_id] = config_.init_default_troops;
        }
    }

    // Initialize edge lanes
    edge_lanes_.resize(graph_.num_edges());
    for (const Edge& e : graph_.edges) {
        EdgeLanes& el = edge_lanes_[e.idx];
        el.edge_idx = e.idx;
        el.node_a = e.a_idx;
        el.node_b = e.b_idx;
        el.edge_length = e.length;
    }
}

void Game::set_node_state(int node, NodeState state, int owner, int troops) {
    NodeData& nd = node_data_[node];
    nd.state = state;
    // Clear existing troops and accumulated damage
    for (int p = 0; p < n_players_; p++) {
        nd.troops[p] = 0;
        nd.accumulated_damage[p] = 0.0f;
    }
    if (owner >= 0 && owner < n_players_) {
        nd.owner = owner;
        nd.troops[owner] = troops;
    } else {
        nd.owner = -1;
    }
}

void Game::tick(float dt, const std::vector<PlayerCommands>& commands) {
    // 1. Build
    process_build_commands(commands);
    // 2. Send troops
    process_troop_sends(commands);
    // 3. Retreats
    process_retreats(commands);
    // 4. Update edge lanes (advance, aggregate, collide)
    // 5. Process arrivals
    update_all_edge_lanes(dt);
    // 6. Combat
    resolve_all_combat(dt);
    // 6b. Update ownership based on troop presence
    update_ownership();
    // 7. Production (scaled by dt for game speed)
    produce_all_troops(dt);
    // 8. Update alive
    update_alive();

    time_ += dt;
}

int Game::building_cost(NodeState state) const {
    switch (state) {
        case NodeState::FACTORY:    return config_.cost_factory;
        case NodeState::POWERPLANT: return config_.cost_powerplant;
        case NodeState::FORT:       return config_.cost_fort;
        case NodeState::ARTILLERY:  return config_.cost_artillery;
        default: return 0;
    }
}

bool Game::validate_build(int player_id, const BuildCommand& cmd) const {
    if (cmd.node_idx < 0 || cmd.node_idx >= graph_.num_nodes()) return false;
    const NodeData& nd = node_data_[cmd.node_idx];
    if (nd.owner != player_id) return false;
    if (nd.state == cmd.structure) return false; // already this type
    if (nd.state == NodeState::CAPITAL) return false; // can't build over capital
    int cost = building_cost(cmd.structure);
    if (cost <= 0) return false;
    if (nd.troops[player_id] < cost + 1) return false; // must keep at least 1 troop to hold ownership
    return true;
}

bool Game::validate_troop_send(int player_id, const TroopCommand& cmd) const {
    if (cmd.from_node < 0 || cmd.from_node >= graph_.num_nodes()) return false;
    if (cmd.to_node < 0 || cmd.to_node >= graph_.num_nodes()) return false;
    if (cmd.count <= 0) return false;
    const NodeData& nd = node_data_[cmd.from_node];
    if (nd.troops[player_id] < cmd.count) return false;
    return true;
}

void Game::process_build_commands(const std::vector<PlayerCommands>& commands) {
    for (int p = 0; p < n_players_; p++) {
        if (!alive_[p]) continue;
        for (const BuildCommand& cmd : commands[p].builds) {
            if (!validate_build(p, cmd)) continue;
            int cost = building_cost(cmd.structure);
            node_data_[cmd.node_idx].troops[p] -= cost;
            node_data_[cmd.node_idx].state = cmd.structure;
        }
    }
}

void Game::process_troop_sends(const std::vector<PlayerCommands>& commands) {
    for (int p = 0; p < n_players_; p++) {
        if (!alive_[p]) continue;
        for (const TroopCommand& cmd : commands[p].troops) {
            if (!validate_troop_send(p, cmd)) continue;

            // Find the first hop via routing
            int first_hop = next_hop(graph_, cmd.from_node, cmd.to_node);
            if (first_hop < 0) continue;

            // Find the edge between from_node and first_hop
            int eidx = graph_.edge_between(cmd.from_node, first_hop);
            if (eidx < 0) continue;

            // Deduct troops from node
            node_data_[cmd.from_node].troops[p] -= cmd.count;

            // Insert into edge lane
            insert_troop_group(edge_lanes_[eidx], cmd.from_node, p, cmd.count,
                               cmd.to_node);
        }
    }
}

void Game::process_retreats(const std::vector<PlayerCommands>& commands) {
    for (int p = 0; p < n_players_; p++) {
        if (!alive_[p]) continue;
        for (const RetreatCommand& cmd : commands[p].retreats) {
            // Retreat all groups owned by this player that originated from this node
            // across all edges adjacent to this node
            if (cmd.node_idx < 0 || cmd.node_idx >= graph_.num_nodes()) continue;
            const Node& node = graph_.nodes[cmd.node_idx];
            for (int nbr : node.neighbor_indices) {
                int eidx = graph_.edge_between(cmd.node_idx, nbr);
                if (eidx < 0) continue;
                EdgeLanes& el = edge_lanes_[eidx];
                // Determine which lane this node is the origin of
                int lane_idx = (cmd.node_idx == el.node_a) ? 0 : 1;
                for (auto& g : el.lanes[lane_idx].groups) {
                    if (g.owner == p && !g.retreating) {
                        g.retreating = true;
                    }
                }
            }
        }
    }
}

void Game::update_all_edge_lanes(float dt) {
    std::vector<Arrival> arrivals;
    for (EdgeLanes& el : edge_lanes_) {
        update_edge(el, dt, config_, arrivals);
    }
    process_arrivals(arrivals);
}

void Game::process_arrivals(std::vector<Arrival>& arrivals) {
    for (const Arrival& a : arrivals) {
        // Deposit troops at the arrival node
        node_data_[a.arrived_at_node].troops[a.owner] += a.count;

        // If this isn't the global destination (and routing info exists), route onward
        if (a.global_dest_node >= 0 && a.arrived_at_node != a.global_dest_node) {
            int hop = next_hop(graph_, a.arrived_at_node, a.global_dest_node);
            if (hop >= 0) {
                int eidx = graph_.edge_between(a.arrived_at_node, hop);
                if (eidx >= 0) {
                    // Deduct and re-send
                    node_data_[a.arrived_at_node].troops[a.owner] -= a.count;
                    insert_troop_group(edge_lanes_[eidx], a.arrived_at_node,
                                       a.owner, a.count, a.global_dest_node);
                }
            }
        }
    }
}

void Game::resolve_all_combat(float dt) {
    for (int i = 0; i < graph_.num_nodes(); i++) {
        resolve_combat(node_data_[i], i, graph_, node_data_, n_players_, config_, dt);
    }
}

void Game::produce_all_troops(float dt) {
    // Accumulate dt and only produce when a full tick has elapsed.
    // This maintains production rate regardless of game speed:
    // - At 1x speed (dt=1.0): produce every frame
    // - At 2x speed (dt=2.0): produce once per frame (2 ticks worth)
    // - At 0.5x speed (dt=0.5): produce every 2 frames (1 tick worth)
    accumulated_production_time_ += dt;

    while (accumulated_production_time_ >= 1.0f) {
        produce_troops(node_data_, graph_, config_);
        accumulated_production_time_ -= 1.0f;
    }
}

void Game::update_ownership() {
    for (NodeData& nd : node_data_) {
        int sole_owner = -1;
        int n_present = 0;
        for (int p = 0; p < n_players_; p++) {
            if (nd.troops[p] > 0) {
                sole_owner = p;
                n_present++;
            }
        }
        // Owned iff exactly one player has troops; contested or empty = -1
        nd.owner = (n_present == 1) ? sole_owner : -1;
    }
}

void Game::update_alive() {
    for (int p = 0; p < n_players_; p++) {
        if (!alive_[p]) continue;

        // A player is dead if they have no troops anywhere (nodes + travelling)
        bool has_troops = false;

        // Check nodes
        for (const NodeData& nd : node_data_) {
            if (nd.troops[p] > 0) { has_troops = true; break; }
        }

        // Check edge lanes
        if (!has_troops) {
            for (const EdgeLanes& el : edge_lanes_) {
                for (int lane = 0; lane < 2; lane++) {
                    for (const TroopGroup& g : el.lanes[lane].groups) {
                        if (g.owner == p && g.count > 0) {
                            has_troops = true;
                            break;
                        }
                    }
                    if (has_troops) break;
                }
                if (has_troops) break;
            }
        }

        alive_[p] = has_troops;
    }
}

bool Game::is_game_over() const {
    int alive_count = 0;
    for (int p = 0; p < n_real_players_; p++) {
        if (alive_[p]) alive_count++;
    }
    return alive_count <= 1;
}
