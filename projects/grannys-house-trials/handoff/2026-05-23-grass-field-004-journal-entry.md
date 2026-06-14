# Journal Entry — 2026-05-23

## Summary

Today we completed and verified the `grass-field-004` visual map pass.

The new subproject is wired into CMake, uses `grass-field-003` Step 10 as the
technical baseline, and replaces the old map content with an authored Granny's
House layout at one-inch render/sim resolution over a foot-quantized ground.

## What Was Implemented

- Added `subprojects/grass-field-004` as a build target.
- Switched the runtime simulation to `GrannyMapVisualSim`.
- Set map sizing to a `108 x 156` foot footprint expanded to `1296 x 1872`
  one-inch cells.
- Kept hybrid detail behavior: broad ground stays coarse-like, structures and
  key features use inch detail.
- Added and propagated per-cell `material_id` through CPU buffers and all three
  renderer paths (raycast, split LOD, wireframe).
- Added material color coding in shaders.
- Added selected-column inspector material labeling and foot-cell reference.
- Kept this pass visual-only (no scenario action/anchor wiring yet).

## Validation Performed

- Re-read required project docs and handoff context.
- Verified Grass Field 4 integration points in source and CMake.
- Rebuilt target successfully:
  - `cmake --build build --target grannys_house_trials_grass_field_004`
- Smoke-launched executable for several seconds; no startup crash observed.

## Notes

- `subprojects/grass-field-004/PLAN.md` still contains older planning text
  referencing `SimpleErosionSim`; implementation is correctly on
  `GrannyMapVisualSim`.
- Later in the same day, controls/resizing/picking were extended and documented
  in:
  - `handoff/2026-05-23-grass-field-004-input-resize-and-picking-handoff.md`
  - `subprojects/grass-field-004/lesson_2026-05-23_input_resize_picking.md`
