# Session 0004: Theme effects and RL scoping
Date: 2026-02-03

## Done
- Added theme-specific visual effects system (EffectFlags bitfield on ColorScheme)
  - Cyberpunk: additive neon glow around owned nodes (DrawCircleGradient + BLEND_ADDITIVE)
  - Terminal: CRT scanline overlay (horizontal lines every 3px)
  - Retrowave: attempted multi-stop sunset gradient + glow + noise overlay, scrapped — looked too intense/3D for the top-down perspective
- Fixed noise tile seam (1px vertical line) by switching from manual tile loop to DrawTexturePro with TEXTURE_WRAP_REPEAT
- Made game window resizable (FLAG_WINDOW_RESIZABLE, dynamic GetScreenWidth/Height per frame)
- Updated headless runner to use AttentionAI instead of PassiveAI, added chrono profiling
- Profiled headless: ~415k ticks/sec with AttentionAI on 28 nodes / 63 edges. Game finishes in ~6ms.

## Decisions
| Decision | Verdict | Reason |
|----------|---------|--------|
| Retrowave effects | Scrapped | Gradient looked too saturated/3D, didn't sit well as a background. Could revisit with a subtler approach. |
| Effect system design | Bitflags (not enum) | Allows combining effects (e.g. glow + gradient). Retrowave was using EFFECT_GLOW \| EFFECT_GRADIENT_BG before being scrapped. |
| Scanline implementation | Simple overlay, no shader | Drawing thin rectangles every 3px is fast and avoids shader loading/RenderTexture complexity. |
| Noise tile seam fix | DrawTexturePro with repeat wrapping | Single draw call, GPU handles tiling, no seams from integer truncation gaps. |

## RL / GNN Direction

> "the idea for action space was to have it be driven by a GNN. each node has a vector state and this gets mapped onto an orchestrator (a la 'attention' in the current model). this way its sort of baked into each node how to behave"

Key insight from Joel: **max-cut is equivalent to solving for max economy in this game**. The powerplant/factory placement problem is:
- Powerplants produce nothing, but give +2 to each adjacent factory/capital
- So the optimal build pattern partitions the graph into two sets (factory nodes vs powerplant nodes)
- Value comes from edges crossing the partition (powerplant adjacent to factory)
- This is exactly weighted max-cut
- Which side is factories vs powerplants is trivial — the larger partition gets factories

**Plan**: build a GNN sandbox solving max-cut first, then port the architecture to the full game. Max-cut gives:
- Known optimal solutions to validate against
- Same graph structure and local-decision pattern
- Fast iteration without temporal dynamics or opponent modeling
- The node embedding -> local decision pattern transfers directly to game policy

Mentioned MPI for distributed graph inference at scale.

## Deferred
- Retrowave visual effect — needs a different approach if revisited
- pybind11 bindings for game engine (will be needed eventually for RL training)
- GNN max-cut sandbox (next concrete step when ready to tinker)

## Next
- Set up PyTorch Geometric sandbox for GNN max-cut
- Small random graphs, train GNN to predict per-node partition assignments
- Validate against known optimal cuts before attempting game integration
