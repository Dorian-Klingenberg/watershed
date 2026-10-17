# WebGPU Fluid Sandbox — Questions and Answers

This document records the questions and clarifications discussed while planning
the proposed sandbox. Answers are the user's stated direction unless identified
as a recommendation. Open items are collected at the end. No implementation has
been authorized.

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

**Recommendation so far:** TypeScript is a promising choice for a structured
WebGPU backend because its types can help describe resource and API shapes.
This is a recommendation, not an accepted decision.

### Should domain concepts live in JavaScript/TypeScript?

**Answer:** No. The user does not want domain concepts stuck in JavaScript.
GPU work—whether DirectX or WebGPU—is considered a suitable platform boundary,
not a domain concept.

**Still to clarify:** How the CPU reference, GPU compute kernel, and canonical
simulation contract relate, since the GPU kernel must implement the numerical
operation it executes.

### Where should the new work live?

**Answer:** In its own clean location, with no code dependencies on prior work.
Prior work can be recreated and lessons carried forward.

**Still open:** Exact directory/project name and long-term location. A separate
web-oriented project has been suggested; no name has been approved.

## Scope and interaction

### Which simulation should be first?

**Answer:** The cellular-fluid experiment. More advanced fluid simulations can
be brought in later.

### How much interaction is wanted initially?

**Answer:** Limited interaction. The first iteration is about getting the
platform working and proving that a fluid simulation can run. A minimum set of
controls would be helpful.

**Recommendation so far:** Reset, add a small center pour, and step. The exact
controls have not been confirmed.

### Is this intended to become a broad sandbox immediately?

**Answer:** No. Current concern is the fluid simulation, starting with the
cellular model.

### What are the success criteria?

**Answer:** Not defined yet. The user expects to recognize what they want after
seeing an early iteration.

## Rendering and comparison

### Should the existing D3D12 rendering architecture be copied directly?

**Answer:** Not decided. The user noted that the existing renderer is
unconventional and that reviewing its renderers and shaders should inform
discussion of the system boundary and validation.

**Review finding:** The renderer uses a full-screen triangle and a pixel-shader
raycast that traverses variable-height columns with DDA. Fluid state separates
terrain from fractional water depth. The existing GPU Phase 1 reads back results
to feed the established renderer; Phase 2 keeps water GPU-resident and uses a
matching renderer, with readback for snapshots and edits.

### Should the new visualization reproduce that raycast immediately?

**Answer:** Still open. Options discussed were implementing the raycast path in
WGSL immediately or using a simpler temporary display while validating compute.

### How should CPU and GPU fluid results be compared?

**Answer:** Still open. A CPU reference and checkpoint comparisons were
recommended. Numeric tolerance and any water-conservation checks were proposed,
but not agreed.

### Must comparison be bit-exact?

**Answer:** Still open. Bit-exact comparisons may be too strict across GPU
implementations; tolerance-based comparison was suggested.

## User interface

### Should the UI use Dear ImGui?

**Answer:** Probably not; the user is quite sure it would not be Dear ImGui.

### What UI approach is preferred?

**Answer:** Fluent UI Blazor controls arranged around the WebGPU visualization
pane, rather than floating controls.

**Still open:** Exact composition and which controls/status are worth showing.

## Questions remaining

1. Should the first visual iteration implement the existing column-raycast
   renderer, or use a temporary simpler view?
2. What is the smallest acceptable control set? Are reset, center pour, and
   manual stepping right?
3. What C#↔GPU contract keeps domain concepts out of the browser module while
   still making WGSL an accurate implementation of the fluid algorithm?
4. Does C# keep a CPU reference implementation in the first slice, or is the
   reference used only for offline/checkpoint validation?
5. Which data stays GPU-resident, and when are snapshots transferred back?
6. What comparison policy should be used: exact bits, tolerances, conservation
   invariants, or some combination?
7. Is TypeScript accepted for the browser module, or should it be plain
   JavaScript?
8. What should the first Fluent UI layout contain?
9. What is the final project name and directory location?
10. What should count as a successful first iteration? The user has explicitly
    left success criteria undefined until there is something visible to assess.
