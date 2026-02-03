# Session 0005: Collision Detection Fix & Human Player Input

Date: 2026-02-03

## Done

### Collision Detection Bug Fix
- **Root cause**: `resolve_collisions()` only checked frontmost groups from each lane. Trailing groups could cross without detection.
- **Solution**: Changed to check **all pairs** of (fwd, bwd) groups for collisions. If any pair has `fwd_pos > bwd_pos`, force weaker group to retreat.
- **Equal-size tie-breaker**: Deterministic (uses `node_a + node_b`) to ensure reproducibility for RL training.
- **Invariant verified**: Created randomized stress test (`test_collision_invariant.cpp`) with 1000 ticks, random troop sizes/directions. **1939 violations detected before fix → 0 violations after.**

### Production Rate Invariant
- **Issue**: Changing game speed (via `game_speed` multiplier) affected production rate in real time.
- **Solution**: Added `accumulated_production_time_` to Game class. `produce_all_troops(dt)` accumulates `dt` and only calls `produce_troops()` when accumulated >= 1.0 tick, then decrements.
- **Result**: Production per simulated tick is constant regardless of game speed:
  - 1x speed: produce once/frame
  - 2x speed: produce ~twice/frame
  - 0.5x speed: produce every ~2 frames
- **Key insight**: Production should scale on ticks, not on real time, when slowing down. Must use frequency modulation (only produce when accumulated tick >= 1.0).

### Human Player Input System
- Created `HumanPlayer` class implementing `PlayerInterface`
- **Persistent multi-select**: Hold selection across multiple commands (no awkward clearing)
- **Drag-to-circle selection**: Drag to draw circle, select all owned nodes inside on release. White outline shows selection.
- **Alt-drag to deselect**: Alt+drag draws red circle, deselects nodes in radius.
- **Click-to-toggle**: Click node to add/remove from selection. Alt+click to deselect. Click empty space to clear all.
- **TroopSendConfig struct**: Customizable hotkey amounts
  - Q: fixed (default 50)
  - E: fixed (default 250)
  - R: percentage (default 50%)
  - F: percentage (default 100%)
- **Multi-source sends**: Send from all selected nodes to hovered target. Distribute troops proportionally.
- **Building on all**: B/P/X/C builds factory/powerplant/fort/artillery on all selected nodes.
- **UI rendering**: White outlines on selected nodes, dotted circle while dragging.

### Integration with main_game.cpp
- Added `human_player` for player 0, `AttentionAI` for player 1
- Call `process_input()` every frame (before tick decision)
- Call `human_player->render()` after main game rendering for UI feedback
- Updated help text with new controls

## Decisions

| Decision | Verdict | Reason |
|----------|---------|--------|
| Collision detection scope | Check ALL pairs, not just frontmost | Trailing groups were slipping through. Only checking frontmost is O(1) but incomplete. |
| Equal-size tie-breaker | Deterministic (node_a + node_b) % 2 | Fair, reproducible for RL, avoids randomness bias in single game runs. |
| Production accumulation | Accumulate dt, produce on tick boundary | Maintains invariant: production per tick stays constant. Frequency modulation for slow-down. |
| Multi-select persistence | Keep selection across commands | Better UX; allows issuing multiple commands from same set without reselecting. |
| Drag-to-circle UX | Drag distance = selection radius | Intuitive: longer drag = bigger circle. Small drag (<5px) treated as click. |
| Send from multiple nodes | Distribute sequentially by node index | Simple; avoids complexity of balancing/fairness algorithms. First node sends first. |
| Hotkey for build | X/C instead of F/A | F was taken by troop send (50% garrison). P was powerplant. B=factory. X=fort. C=artillery. |

## Stress Test Coverage

Created `test_collision_invariant.cpp` with randomization:
- **Random troop sizes**: 20-200 per send (not fixed waves)
- **Random send intervals**: 5-20 ticks (not regular 40-tick waves)
- **Random directions**: Alternates, but at random times
- **1000 tick run**: ~76 sends, ~8700 troops total
- **Invariant**: Groups that have crossed MUST have `forced_retreat == true`

Before collision fix: **1939 violations**. After: **0 violations**.

## Deferred

- **Visual feedback for troop sends**: No indication where troops are going; could add arrow/line from source to target.
- **Selection feedback in HUD**: Could show "Selected: 5 nodes" in HUD.
- **Hotkey rebinding UI**: TroopSendConfig is struct; could expose via in-game menu later.
- **Fog of war**: UI is full vision; spectator mode. Can add partial vision per player later.
- **Retreat visibility**: Retreating groups render with reduced alpha, but hard to see at zoom. Could highlight differently.

## Next

1. **Test multiplayer**: Play a full game human vs AttentionAI. Verify input feels responsive, no edge cases.
2. **Visual polish**: Add arrows for troop sends, or highlight path.
3. **Performance**: Profile at high troop counts / large graphs. Collision checking is now O(n_groups^2) per edge; may need optimization.
4. **GNN integration**: Next phase per session 0004 plan — build PyTorch Geometric max-cut sandbox.
5. **Game balance**: Tweak production rates, building costs, fort/artillery multipliers based on play experience.

## Files Modified/Created

```
NEW:
  src/player/human_player.hpp
  src/player/human_player.cpp
  tests/test_collision_invariant.cpp
  claude/sessions/0005-collision-fix-and-human-input.md

MODIFIED:
  src/engine/edge_lanes.cpp           — rewrote resolve_collisions()
  src/engine/production.hpp/cpp        — removed dt scaling (tick-based instead)
  src/engine/game.hpp/cpp              — added accumulated_production_time_
  src/main_game.cpp                    — integrated HumanPlayer, updated UI
  CMakeLists.txt                       — added test_collision_invariant, human_player.cpp
```

## Technical Debt / Known Gaps

- **Collision detection O(n_groups^2)**: For large maps with many groups per edge, consider spatial hashing or sorted-list comparison.
- **Production fraction accumulation**: Currently uses float; no precision loss handling for very long games (would need double or accumulator).
- **Multi-node send distribution**: Currently sequential by node index; no "fairness" logic if some nodes run out of troops mid-send.
- **Drag UI at zoom extremes**: Very zoomed out, drag circle may be hard to see. Could scale circle size by zoom.

## Context for Next Agent

**The game is playable!** You can now sit down and play a real game as the red player against an attention-based bot. The engine is deterministic, tests are comprehensive, and core mechanics work.

Key architectural facts:
- Game loop uses fixed `dt` (normally 1.0 tick/frame), multiplied by `game_speed` for frame-level control.
- Production happens at tick boundaries via accumulation (not per-frame).
- Collision invariant: `if fwd_pos > bwd_pos (crossed) then at_least_one.forced_retreat == true`.
- HumanPlayer maintains `selected_nodes_` (set) and processes input each frame, generates commands each tick.

If continuing: consider either gameplay balance tuning or RL integration next.
