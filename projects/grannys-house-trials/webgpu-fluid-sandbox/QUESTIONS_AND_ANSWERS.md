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
- Use GitHub Pages as a free static deployment target.
- Keep the project independent of previous code and keep domain concepts in C#;
  the JavaScript module is a browser/GPU adapter.
- Do not require a server backend, advanced solver, or the prior raycast
  renderer for this first slice.

## Platform and project shape

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

## Questions remaining after the first implementation

1. After reviewing the first visual iteration, what should count as a useful
   next improvement?
2. Does the regular cell map communicate enough about flow before any 3D or
   raycast renderer work is considered?
3. Should a future version add continuous stepping, additional controls, or
   more fluid models?
