# WebGPU Fluid Sandbox — Questions and Answers

This document records questions and clarifications discussed while planning the
sandbox. Answers are the user's stated direction unless identified as a
recommendation. The initial implementation has now been approved and built; the
current confirmed scope is recorded below.

## Confirmed first implementation

- Build the application in this directory as a standalone Blazor WebAssembly app.
- Use Fluent UI Blazor controls, WebGPU compute, and a regular 2D cell view.
- Include both the C# CPU reference and the WGSL GPU implementation from the
  beginning.
- Include reset, center pour, manual step, and explicit CPU/GPU comparison.
- Drain water at the field boundary after each tick.
- Use GitHub Pages as a free static deployment target.
- Keep the project independent of previous code and keep domain concepts in C#;
  the JavaScript module is a browser/GPU adapter.
- Do not require a server backend, advanced solver, or the prior raycast
  renderer for this first slice.

## Platform and project shape

### Approved algorithm-selector follow-up

Four real numeric models now run on both C# and WebGPU: Cellular, Pipe-flux
shallow water, Terrain-head pipe flow, and staggered MAC shallow water. The
terrain selector is independent of the algorithm; execution optimizations and
historical scenario names are not presented as extra algorithms.

Changing algorithm pauses and clears depths, face rates/velocities and ticks,
but preserves the current edited terrain and camera. Reset instead restores the
starting scene while retaining the chosen algorithm. Complete-state comparison,
reference re-enable and GPU-failure recovery include all face state, not just
water depths. Pipe-flux uses linearly damped volume rates, Terrain-head adds
depth-dependent rough-bed friction, and MAC transports donor depth with genuine
face velocity state. MAC velocity advection is now a real optional first-order
upwind stage (default off); other historical/erosion branches remain deferred.
See README for units, numerical safeguards,
comparison tolerance and behavioral references.

### Approved settings and terrain experiments follow-up

C# owns validated immutable settings for each model: cellular max flow/settle,
pipe area/retention, terrain-head area/roughness, MAC damping/advection. Only
relevant controls appear. Changes pause and reset complete fluid state without
discarding terrain edits or camera; restore defaults affects the current model.
Synchronization/comparison includes settings identity.

The scene picker provides slope, enclosed basin, water slide, and a flat-bottom
Tesla channel with six alternating hooked returns on the 1-ft grid. Reset restores
the selected terrain. Forward/reverse experiment controls
restore identical initial wetting and swap fixed 18/12-in reservoir heads.
C# generates those constraints; GPU kernels and CPU implement the same forcing.
CPU-off still skips CPU reference state work.

Discharge and bypass throughput use signed actual face transfers. Momentum
models divide by their 0.016-s tick and report ft³/s. Cellular has no physical
tick duration and instead reports ft³/tick; no physical rate is implied.
They are not circulation/vorticity or guaranteed steady flow.
The depth-averaged scene is an experimental passive-geometry test, not a promise
of check-valve closure or directional resistance. README records measured
equal-time forward/reverse results, including essentially symmetric pipe-flux flow.

### Approved interactive follow-up

The user subsequently approved Run/Pause alongside the existing controls,
an interesting downhill landscape with a channel and collecting area, and
barriers for pooling, diversion, and overflow. Other solvers can reuse this scene;
realistic waves are not required from the current solver.

Both views share camera, selection, hover, and edit previews. Left click selects,
left drag orbits about the initially hovered terrain point, the wheel zooms about
the hovered point, right click pours, and right drag previews then commits a
barrier. Identical edits apply to each implementation's independent water state.
Two-axis landscape navigation is linked (horizontal below, depth-axis slider on
the physical right), while vertical page scrolling and
responsive layout keep the workbench usable on small screens.

The current renderer uses Canvas2D-projected columns rather than the original
flat cell map; WebGPU remains the compute backend, not a distinct renderer.

### Approved performance follow-up

The CPU reference can now be disabled completely without abandoning C# domain
ownership. Its solver, water snapshots, statistics, and drawing stop; comparisons
are disabled. Re-enabling explicitly imports a fresh GPU baseline and tick,
announces that boundary, then returns to independent stepping and additive edits.
GPU failure restores the last completed GPU display baseline into CPU fallback
(or explicitly resets if recovery fails), rather than resuming stale CPU water.

Run uses display-paced configurable tick batches instead of a fixed delay.
Readouts distinguish actual dirty-draw FPS, ticks/sec, CPU/draw costs, and the
GPU dispatch/readback wall-clock roundtrip; the latter is not actual GPU time.
Cached display water serves selection without extra GPU reads.

Workers remain an assessed option, not a requirement. Message-passing workers do
not themselves require COOP/COEP, but shared-memory WebAssembly threads do, and
this Pages deployment must not assume cross-origin isolation. A C# worker would
also require a separate runtime/entry and explicit state ownership. See README.

### What kind of WebGPU application is intended?

**Answer:** A browser application built with Blazor WebAssembly. The user's
clients use Blazor, making it a relevant platform to learn.

### Should JavaScript interop be avoided entirely?

**Answer:** No. The user expects some JavaScript interop, but wants it limited as
much as humanly possible.

### Should the browser GPU object or objects be JavaScript or TypeScript?

**Answer:** The browser-side GPU concepts should be fully implemented in
JavaScript or TypeScript. The user is open to guidance about the language and
does not yet know TypeScript.

**Implementation:** The initial browser adapter is plain JavaScript. TypeScript
remains a possible later change, not a requirement for this first slice.

### Should domain concepts live in JavaScript/TypeScript?

**Answer:** No. The user does not want domain concepts stuck in JavaScript.
GPU work—whether DirectX or WebGPU—is considered a suitable platform boundary,
not a domain concept.

**Implementation:** C# owns the domain-facing model and reference state. The
GPU kernel necessarily implements the same numeric flow rule in WGSL. The
browser-side JavaScript owns WebGPU resource operations, not domain actions.

### Where should the new work live?

**Answer:** In its own clean location, with no code dependencies on prior work.
Prior work can be recreated and lessons carried forward.

**Current location:** `projects/grannys-house-trials/webgpu-fluid-sandbox/`.
The product name remains provisional.

## Scope and interaction

### Which simulation should be first?

**Answer:** The cellular-fluid experiment. More advanced fluid simulations can
be brought in later.

### How much interaction is wanted initially?

**Answer:** Limited interaction. The first iteration is about getting the
platform working and proving that a fluid simulation can run. A minimum set of
controls would be helpful.

**Implementation:** Reset, add a 12-inch center pour over a small radius, and
advance both implementations by one manual tick.

### Is this intended to become a broad sandbox immediately?

**Answer:** No. Current concern is the fluid simulation, starting with the
cellular model.

### What are the success criteria?

**Answer:** Not defined yet. The user expects to recognize what they want after
seeing an early iteration.

## Rendering and comparison

### Should the existing D3D12 rendering architecture be copied directly?

**Implementation:** Use a regular 2D cell map for the first iteration. Revisit
the raycast after the fluid behavior is useful and inspectable.

**Review finding:** The renderer uses a full-screen triangle and a pixel-shader
raycast that traverses variable-height columns with DDA. Fluid state separates
terrain from fractional water depth. The existing GPU Phase 1 reads back results
to feed the established renderer; Phase 2 keeps water GPU-resident and uses a
matching renderer, with readback for snapshots and edits.

### Should the new visualization reproduce that raycast immediately?

**Implementation:** No. Use a regular 2D cell map first and revisit the
raycast only if the initial field view proves insufficient.

### How should CPU and GPU fluid results be compared?

**Implementation:** Compare CPU and GPU cell depths on request using an
absolute per-cell tolerance of `0.0001`, and report total-water difference.

### Must comparison be bit-exact?

**Implementation:** Use tolerance-based comparison rather than requiring
bit-exact agreement.

## User interface

### Should the UI use Dear ImGui?

**Answer:** Probably not; the user is quite sure it would not be Dear ImGui.

### What UI approach is preferred?

**Answer:** Fluent UI Blazor controls arranged around the WebGPU visualization
pane, rather than floating controls.

**Implementation:** The current UI presents two field views, basic controls,
simulation readouts, and explicit comparison feedback.

## Questions remaining after the interactive follow-up

1. Which fluid solver should be evaluated next against the same landscape?
2. Is GPU-resident rendering warranted after assessing the projected-column views?
3. Which additional browser or touch/stylus interactions should be supported?
