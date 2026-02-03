# RiskC++ Strats

A C++ remake of [Risky Strats](https://www.roblox.com/games/4278596942/Risky-Strats) (Roblox) — a real-time strategy game played on geometric graphs. Built as both a playable game and a headless environment for reinforcement learning.

![Screenshot](screenshots/theme_00_Default.png)

![All 17 themes](screenshots/collage.png)

## The Game

Players control territory on a graph. Nodes produce troops, troops travel along edges, and combat resolves when opposing forces share a node. Build structures to boost production, strengthen defenses, or project power.

**Structures** (built by spending troops):
- **Factory** (500) — produces 1 troop/tick
- **Powerplant** (2500) — boosts each neighboring factory/capital by +2 troops/tick
- **Fort** (400) — 1.2x defense multiplier for owner
- **Artillery** (4000) — 1.5x attack multiplier at neighboring nodes
- **Capital** — starting node, produces 2 troops/tick

**Troop movement**: larger groups move slower (`speed = c / cbrt(count)`). Opposing groups on the same edge force the smaller group to retreat. Routing uses greedy dot-product similarity toward a global target.

**Combat**: continuous attrition when multiple players share a node. Attack scales at `troops/10`, defense at `troops/100`.

## Building

Requires CMake 3.20+ and a C++20 compiler. RayLib 5.5 is fetched automatically.

```sh
cmake -B build
cmake --build build
```

This produces:
- `crisky_game` — interactive game (human vs AttentionAI)
- `crisky_headless` — no-GUI simulation for RL training
- `test_*` — test suite

## Playing

```sh
./build/crisky_game [seed] [--scheme=Nord]
```

### Controls

| Key | Action |
|-----|--------|
| **Click** | Select node (clears previous) |
| **Shift+Click** | Add/toggle node in selection |
| **Alt+Click** | Remove node from selection |
| **Drag** | Circle select (replaces selection) |
| **Shift+Drag** | Circle select (adds to selection) |
| **Alt+Drag** | Circle deselect |
| **Q / E** | Send 501 / 2501 troops to hovered node |
| **R / F** | Send 50% / 100% of garrison to hovered node |
| **1 / 2 / 3 / 4** | Build Factory / Fort / Powerplant / Artillery on hovered node |
| **WASD** | Pan camera |
| **I / O** | Zoom in / out |
| **K / L** | Rotate camera |
| **[ / ]** | Cycle color themes |
| **+/-** | Game speed |
| **Space** | Pause |

17 color themes included (Default, Cyberpunk, Solarized, Dracula, Nord, Retrowave, Terminal, etc.).

## Architecture

```
src/
  engine/          # Game logic (no rendering dependencies)
    game.cpp       # Tick loop: production, combat, movement, routing
    graph.cpp      # Poisson process graph generation
    edge_lanes.cpp # Troop transport along edges
    combat.cpp     # Attrition combat resolution
    production.cpp # Troop generation from structures
    routing.cpp    # Greedy dot-product routing
  renderer/        # RayLib rendering
    renderer.cpp   # Layered drawing (edges, troops, nodes, halos, labels)
    camera.cpp     # Pan/zoom/rotate camera
    color_scheme.hpp # 17 color themes with SYS color auto-contrast
  player/
    human_player.cpp   # Mouse/keyboard input, selection, commands
    attention_ai.cpp   # Attention-mechanism AI opponent
```

The game tick is a single `game.tick(dt, commands)` call. AI players implement `PlayerInterface::decide()`.
