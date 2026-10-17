# WebGPU Fluid Sandbox — Deferred To-Do Ledger

The first runnable slice is implemented. This ledger contains follow-up work
that remains intentionally outside its scope.

## Deferred

- [ ] Review the first field view and decide what would make the simulation more
  useful or understandable.
- [ ] Consider a GPU-resident renderer or the existing column-raycast approach
  only after assessing the regular 2D cell map.
- [ ] Consider continuous stepping, more controls, and additional fluid models
  after the manual cellular-flow loop is reviewed.
- [ ] Evaluate additional browsers after the initial Chrome target.
- [ ] Decide whether the provisional project name should change.

## First-slice choices already made

- [x] Use Blazor WebAssembly with Fluent UI Blazor controls.
- [x] Keep the app independent of earlier implementations.
- [x] Implement the cellular-fluid model in both C# and WGSL.
- [x] Use reset, a center pour, manual stepping, and explicit CPU/GPU comparison.
- [x] Start with regular 2D cell rendering.
- [x] Keep domain concepts and comparison behavior in C#; keep WebGPU resources
  behind coarse-grained JavaScript interop.
- [x] Include a GitHub Pages deployment workflow.
