#ifndef CRISKY_COMBAT_SOLVERS_HPP
#define CRISKY_COMBAT_SOLVERS_HPP

#include "ai/models.hpp"

// Combat "solver": wraps the model registry.
// A CombatSolver is a ModelFactory — it creates a PlayerInterface given a player_id.
// This typedef exists so the combat gym has a named concept for its solver slot,
// even though the underlying type is the full-game model factory.
using CombatSolver = ModelFactory;

// Get a combat solver by model name. Returns nullptr if not found.
inline const CombatSolver* get_combat_solver(const std::string& name) {
    return get_model(name);
}

// List all available combat solver (model) names.
inline std::vector<std::string> list_combat_solvers() {
    return list_models();
}

#endif
