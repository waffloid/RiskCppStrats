#ifndef CRISKY_SYSTEMS_TYPES_HPP
#define CRISKY_SYSTEMS_TYPES_HPP

#include <utility>
#include <vector>

#include "player/player_interface.hpp"  // for BuildCommand

// ──────────────────────────────────────────────────────────────
//  Inter-system I/O contract types.
//
//  These define the data that flows between systems.  Each system's
//  output type should be directly usable as input to the next system
//  in the pipeline (or to any system, for extensible couplings).
// ──────────────────────────────────────────────────────────────

// Economy → Ordering: what to build and in what order.
struct BuildPlan {
    std::vector<BuildCommand> steps;   // ordered list of (node, structure) pairs
    float estimated_production = 0.0f; // expected steady-state production if fully executed
};

// Ordering → Transport: desired troop distribution to achieve.
struct DesiredDistribution {
    std::vector<int> target_troops;    // per-node desired troop count
};

// Combat → Transport: troops demanded at specific frontier nodes.
struct TroopDemand {
    std::vector<std::pair<int, int>> demands; // (node_idx, troops_wanted)
};

#endif
