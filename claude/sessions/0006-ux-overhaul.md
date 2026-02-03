# Session 0006: UX Overhaul

Date: 2026-02-03

## Done

### Selection Behavior Rewrite
- **Click** → selects ONLY that node (clears previous selection)
- **Click unowned node or empty** → clears selection
- **Shift+click** → add/toggle node in selection
- **Alt+click** → remove node from selection
- **Drag** → circle select, REPLACES entire selection with enclosed owned nodes
- **Shift+drag** → union enclosed nodes into selection
- **Alt+drag** → remove enclosed nodes from selection
- Added `is_shift_dragging_` tracking alongside existing `is_alt_dragging_`

### Drag Circle Visual
- Replaced thin 1px `DrawCircleLines` with custom `draw_dashed_circle()` (alternating arc segments, 2.5px thickness)
- Normal/shift drag: player color (alpha 160)
- Alt drag: scheme `node_outline` color (alpha 180)

### Hotkey Remapping
- **Troop sends**: Q (501 fixed), E (2501 fixed), R (50%), F (100%) — all leave 1 troop at source
- **Building**: 1=Factory, 2=Fort, 3=Powerplant, 4=Artillery (was B/P/X/C)
- **Camera rotation**: moved from Q/E to K/L
- **Camera zoom**: I/O (was I/O for zoom, briefly swapped around during session)
- **Theme switching**: removed 1-9 and 0 keybinds (freed for building), kept [/] cycling

### Build on Hover
- Building now works on hovered node, no selection required
- Added `pending_build_node_` to track which node
- Engine change: allow rebuilding over existing buildings (except capitals and same-type)

### Rendering Layer System
- Split `draw_nodes()` into `draw_node_circles()` and `draw_node_labels()`
- `Renderer::draw()` now accepts optional `std::set<int>*` of selected nodes
- Layer order: edges → troop groups → node circles/icons → **selection halos** → node labels
- Selection halos rendered as thick `DrawRing` touching node edge, extending outward (25% of node radius)
- Added `HumanPlayer::selected_nodes()` accessor for main_game to pass to renderer

### SYS Color System
- Added `ColorScheme::sys_color()` — computed white or black based on background luminance
- Threshold at 75/255 luminance (non-linear perceptual): 13 dark themes get white, 4 light themes (Desert, Monochrome, Arctic, Candy) get black
- Alpha 128 (50%)
- Used for: selection halos, state icons (replaced per-scheme `state_icon` for icons)

### Text Rendering
- Font sizes doubled: `font_size_troop` 4.8→9.6, `font_size_node_label` 5.6→11.2
- Added `Renderer::draw_outlined_text()` — draws text at 8 surrounding pixel offsets in contrast outline color, then foreground
- Outline color derived from *foreground text* luminance (not background) — light text gets dark outline, dark text gets light outline
- Applied to node troop counts and moving troop group labels

### Troop Mechanics
- **Leave 1 troop**: percentage sends and the `decide()` distribution both cap available troops at `troops - 1`
- **Unowned source constraint**: if any selected source node is unowned by player, target must be owned (prevents routing through no-man's land). Owned sources can send anywhere.
- **Send to selected node**: removed the check preventing sends to nodes in the selection
- **Troop speed**: changed from `c1 + c2/count` to `c2/cbrt(count)` — approaches 0 for large groups (was asymptoting to 0.5)
- **dt**: reduced from 1.0 to 0.25

### Other
- Hover/click detection radius: 20.0 → 1.4 world units (just slightly wider than 1.05 node radius)
- Minimum troop dot radius on edges: 0.3 → 0.1
- Q=501, E=2501 fixed troop amounts

## Decisions

| Decision | Verdict | Reason |
|----------|---------|--------|
| Selection model | RTS-style (click=exclusive, shift=add, alt=remove) | More intuitive than toggle-based; matches user expectations from strategy games |
| SYS color source | Background luminance | Simple, works across all 17 themes. Threshold at 75/255 catches the perceptual non-linearity |
| Text outline source | Foreground text luminance | Outline should contrast with the text it outlines, not the background. Light text on dark bg needs dark outline around the text. |
| Selection halo layer | Above node circles, below labels | Joel: "rings should go above the path but below text" — went through several iterations |
| Build trigger | Hover, not selection | Joel: "I don't need to select something in order to build it, just hover" |
| Troop speed curve | `c2/cbrt(count)` | Joel: "they should go towards 0" then "lets do cube root" — gentler than 1/count, still approaches 0 |
| Camera rotation keys | K/L | Joel: "i/o can be in/out e.g. for zooming in zooming out" — I/O is more mnemonic for zoom |
| Rebuild allowed | Yes (except capitals, same-type) | Joel: "I seem to not be able to re-build on something already built" |

## Iteration Log

Selection halo placement went through several iterations based on feedback:
1. First: drawn in `human_player::render()` above everything (including text) — rejected
2. Second: moved below edges (bottom layer) — Joel: "i take it back, above path but below text"
3. Final: split `draw_nodes` into circles + labels, halos between them

SYS color also iterated:
1. First: based on background luminance, threshold 128, alpha 220
2. Joel: "more themes should have dark sys color, threshold ~75, alpha 0.5"
3. Joel: "should be based off the thing it is outlining" — changed text outline to use fg luminance, kept halos/icons on background luminance

## Deferred

- **`state_icon` field in ColorScheme**: Now unused for icons (replaced by `sys_color()`), but still defined in all 17 schemes. Could remove but low priority.
- **`displacement_c1` in GameConfig**: No longer used in displacement formula. Could remove.
- **Visual feedback for troop sends**: Still no indication of where troops are going.
- **Fog of war**: Still full vision.

## Next

1. **Playtesting**: Play a full game with new controls, verify feel
2. **Game balance**: Tune production rates, costs, speed curve with new dt=0.25
3. **GNN integration**: Per session 0004 plan
4. **Performance**: Profile at high troop counts with new cbrt speed formula

## Files Modified

```
MODIFIED:
  src/engine/edge_lanes.cpp           — troop speed: cbrt(count) instead of c1 + c2/count
  src/engine/game.cpp                 — allow rebuild over existing buildings
  src/main_game.cpp                   — removed 1-9 theme keys, pass selection to renderer, dt=0.25, updated help text
  src/player/human_player.hpp         — new render signature, shift drag, build node, dashed circle, selected_nodes accessor
  src/player/human_player.cpp         — full selection rewrite, hotkey remap, send constraints, build on hover
  src/renderer/camera.cpp             — rotation K/L, zoom I/O
  src/renderer/color_scheme.hpp       — sys_color() method
  src/renderer/renderer.hpp           — split draw_nodes, add draw_selection_halos, draw_outlined_text, font size 2x
  src/renderer/renderer.cpp           — layer system, selection halos, outlined text, sys_color for icons
  claude/CLAUDE.md                    — updated displacement formula and UI docs
```

## Context for Next Agent

All changes committed as `2a65e6c`. No remote configured (local only).

Key architectural additions:
- `ColorScheme::sys_color()` auto-selects white/black contrast color based on background luminance
- `Renderer::draw()` accepts optional `std::set<int>*` for selection halos, rendered at correct layer
- `draw_node_circles()` and `draw_node_labels()` are separate passes, allowing insertion of selection halos between them
- `HumanPlayer::selected_nodes()` exposes selection for the renderer layer system
- Troop displacement is now `c2 / cbrt(count)` — no base speed term
- dt is 0.25 (was 1.0), so everything runs at 1/4 speed per frame
