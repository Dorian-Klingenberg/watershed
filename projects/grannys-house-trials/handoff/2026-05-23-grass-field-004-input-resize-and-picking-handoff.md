# Handoff — 2026-05-23 — Grass Field 004 Input/Resize/Picking

## Scope

This handoff captures all Grass Field 004 work completed today after the
authored-map visual integration:

- input remapping
- live hover highlight
- window resize correctness
- improved picking accuracy
- right-drag pan feel update

All changes were made in:

- `subprojects/grass-field-004/main.cpp`
- `subprojects/grass-field-004/shaders/grass_field_renderer.hlsl`
- `subprojects/grass-field-004/shaders/split_lod_renderer.hlsl`
- `subprojects/grass-field-004/shaders/wireframe_renderer.hlsl`

## User-Requested Behavior

1. Show which column will be selected before click.
2. Keep split coarse/fine as default renderer.
3. Use middle mouse for orbit.
4. Use right-drag for map panning.
5. Restore left-click selection.
6. Make window resizing stable (no UI/picking drift).
7. Improve selection accuracy, especially at angle.
8. Make pan feel track mouse movement better at zoomed-out views.

## Implemented Changes

### A) Input Remap and Selection

- Left-click now selects hovered column.
- Middle-drag now orbits camera.
- Right-drag now pans.
- Camera help text updated in ImGui to match controls.

### B) Hover Highlight

- Added highlight coordinates into `SceneConstants`.
- Passed highlight to all three shader paths.
- Highlight tint is visible in:
  - raycast
  - split LOD
  - wireframe

### C) Resize Handling

- Added deferred resize flow:
  - `WM_SIZE` stores pending dimensions.
  - render loop performs resize safely between frames.
- On resize:
  - waits for GPU
  - releases old back buffers and depth buffer
  - calls `ResizeBuffers`
  - recreates RTVs + depth resource + DSV
  - refreshes `frame_index`
- Guarded zero-sized frame path.

### D) Resize Crash Fix

Initial resize implementation failed because `ResizeBuffers` used a swap-chain
flag not present at creation time.

Fix:
- `ResizeBuffers(..., flags=0)` to match swapchain creation.

### E) Picking Accuracy Upgrade

Replaced old ground-plane (`Y=0`) picking with true CPU ray traversal:

- builds world ray from mouse + inverse VP
- intersects field AABB
- performs DDA traversal through X/Z columns
- does per-cell ray-vs-column-AABB test using actual column top height

Result:
- angled picks align with visible geometry far better than plane projection
- top-down alignment improved as well

### F) Right-Drag Pan Feel Upgrade

Replaced pixel-scaled pan with world-space anchored pan:

- when right drag starts, stores a pan plane height at selected column surface
- each drag frame:
  - builds previous and current mouse rays
  - intersects both with the same horizontal plane
  - applies world delta directly to camera focus offsets

Result:
- map movement tracks cursor motion much more naturally when zoomed out

## Validation Performed

- `cmake --build build --target grannys_house_trials_grass_field_004`
  succeeded after each major change set.
- Startup smoke runs succeeded after:
  - input remap
  - resize path integration
  - resize crash fix
  - picking + pan updates

## Known State

- Grass Field 004 remains visual-first (no scenario action wiring yet).
- Safe swale/check-dam/hidden-drain semantics are currently represented as
  authored visual/material topology, not a full moisture simulation model.

## Suggested Next Calibration (Optional)

If additional fine-tuning is needed:

1. Pan plane source toggle:
   - selected column plane (current)
   - hovered column plane
   - camera focus plane
2. Picking debug overlay:
   - show CPU hit position + traversed cell count for quick calibration.
