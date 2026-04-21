#ifndef CRISKY_PROJECTIONS_HPP
#define CRISKY_PROJECTIONS_HPP

#include "engine/game.hpp"
#include "gyms/economy_gym.hpp"
#include "gyms/combat_gym.hpp"
#include "gyms/transport_gym.hpp"

// Project a live game into an economy gym state for analysis.
EconomyGymState project_economy(const Game& game, int player_id,
                                 const std::string& solver_name = "greedy");

// Project a live game into a combat benchmark for replay or analysis.
CombatBenchmark project_combat(const Game& game);

// Project a live game into a transport preset for analysis.
TransportPreset project_transport(const Game& game, int player_id,
                                   const std::vector<int>& target_troops);

#endif
