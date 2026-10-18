# WebGPU Fluid Sandbox — Deferred To-Do Ledger

The first runnable slice is implemented. This ledger contains follow-up work
that remains intentionally outside its scope.

## Deferred

- [ ] Consider a GPU-resident renderer or the existing column-raycast approach
  after assessing the linked projected-column views.
- [ ] Revisit remaining historical heightfield SWE, virtual-pipe, O'Brien volume-only,
  slosh scenario presets, and hydraulic erosion/rain branches after the four models.
- [ ] Assess longer-time convergence and calibrated Tesla geometries; current
  depth-averaged forward/reverse measurements are experimental, not guaranteed resistance.
- [ ] Evaluate additional browsers after the initial Chrome target.
- [ ] Decide whether the provisional project name should change.
- [ ] Reassess dedicated workers only after profiling; do not assume Pages provides
  COOP/COEP or shared-memory WASM threads (see README hosting assessment).

## First-slice choices already made

- [x] Use Blazor WebAssembly with Fluent UI Blazor controls.
- [x] Add validated per-model settings, restore defaults, and real optional MAC velocity advection.
- [x] Separate terrain selection from algorithms: slope, basin, slide, single-stage Tesla.
- [x] Add repeatable equal-head forward/reverse reservoir trials and actual transfer-rate probes.
- [x] Keep the app independent of earlier implementations.
- [x] Implement the cellular-fluid model in both C# and WGSL.
- [x] Add paired Pipe-flux, Terrain-head and staggered MAC models, preserving the current
  landscape; switch pauses/clears complete fluid state without discarding terrain edits.
- [x] Compare/import/export complete depth and face state for reference re-enable/fallback.
- [x] Use reset, a center pour, manual stepping, and explicit CPU/GPU comparison.
- [x] Start with regular 2D cell rendering.
- [x] Keep domain concepts and comparison behavior in C#; keep WebGPU resources
  behind coarse-grained JavaScript interop.
- [x] Include a GitHub Pages deployment workflow.
- [x] Add Run/Pause, point pours, and barrier strokes without replacing independent GPU water.
- [x] Add height-aware linked views with selection, anchored orbit/zoom, and horizontal navigation.
- [x] Keep controls reachable with vertical page scrolling and responsive layout.
- [x] Add true CPU-reference suspension, fresh-baseline re-enable, and non-stale CPU fallback.
- [x] Add a physically right-side depth slider linked with horizontal navigation and orbit/zoom.
- [x] Measure actual dirty-draw FPS, ticks/sec, draw/CPU cost, and explicitly labeled GPU roundtrip.
- [x] Batch ticks per display, cache display readbacks/terrain/projection/order, reuse CPU delta storage,
  and skip disabled/offscreen canvas drawing without introducing mixed-solver parallelism.
