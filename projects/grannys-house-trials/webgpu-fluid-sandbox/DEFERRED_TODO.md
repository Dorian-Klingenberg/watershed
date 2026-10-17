# WebGPU Fluid Sandbox — Deferred To-Do Ledger

This list records work and decisions discussed but intentionally deferred.
Nothing here authorizes implementation. Priorities are not implied unless
explicitly stated.

## First iteration direction

- [ ] Use regular rendering for the first iteration while establishing a decent
  cellular-fluid simulation.
- [ ] Explore the existing column-raycast rendering approach later, after the
  fluid simulation is working well.

## Resolve before implementation

- [ ] Define the minimal interaction set. Reset, a small center water pour, and
  manual stepping are current candidate controls only.
- [ ] Define the C# and browser GPU boundary, including which side owns active
  simulation state and how coarse-grained calls are shaped.
- [ ] Decide whether to implement the CPU reference alongside the first GPU
  version and how it participates in validation.
- [ ] Choose the CPU/GPU comparison policy: exact comparison, numeric tolerance,
  conservation checks, or another approach.
- [ ] Decide whether the browser GPU module should be TypeScript or JavaScript.
- [ ] Choose the initial Fluent UI layout and which summaries/status to show.
- [ ] Confirm a project name and final directory location.
- [ ] Define success criteria after reviewing an early visual iteration.

## Explicitly later or not part of the first slice

- [ ] Add more advanced fluid simulations, such as the MAC-grid model.
- [ ] Consider GPU-resident rendering and snapshot-based CPU inspection as a
  later refinement if the initial path needs them.
- [ ] Expand beyond Chrome as an initial browser target if and when needed.
- [ ] Add richer simulation controls or a full sandbox workflow only after the
  minimal platform iteration has been seen and assessed.

## Guardrails while planning

- Keep the first effort focused on fluid simulation and platform feasibility.
- Keep the new project independent of prior code; use existing work only as
  behavioral and rendering reference.
- Keep JavaScript interop coarse-grained and limited; avoid per-cell or
  per-frame crossings.
- Do not move domain concepts into the browser GPU module.
- Do not establish success metrics before the user has seen an iteration.
