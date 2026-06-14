# Journal Entry - 2026-06-01

## Summary

Today was a large Grass Field 003 exploration day focused on turning the old
step-based demo into a set of fluid, pipe-flow, and erosion experiments.

The main through-line was: make water visible, make fluid behavior inspectable,
keep experiments separate, then move from toy water flow toward paper-derived
hydraulic erosion.

## Major Work Streams

### Experiment Structure

- Reviewed the Grass Field 003 documentation and clarified the existing process
  for adding a new step/experiment.
- Shifted the mental model from numbered steps to separate named experiments.
- Preserved older experiments rather than replacing them, so cellular flow,
  shallow water, terrain-head pipes, O'Brien-style volume flow, and hydraulic
  erosion can be compared side by side.
- Added and expanded lesson documentation along the way.
- Added sequence interaction diagrams broadly across lessons.
- Recorded the working preference that Dorian loves sequence diagrams.

### Cellular And Shallow Water Experiments

- Added the first simple cellular water-flow experiment.
- Made water cells blue so the water read clearly against the terrain.
- Changed camera orbit behavior so the camera can orbit around the selected
  cell rather than only the field center.
- Explored one-inch grid scale and confirmed the major slowdown comes from
  increasing the active simulation/render cell count, not merely from having
  wireframe initialized.
- Added dirty-flag behavior so projection/view-derived data and field buffers
  are not rebuilt every frame unless needed.
- Investigated obvious frame costs around `update_field_buffer()` and render
  buffer rebuilds.

### Split Coarse/Fine Renderer

- Designed and implemented the split coarse/fine column renderer.
- Kept the simulation grid unchanged while rendering full coarse columns first
  and only rendering fine remainder cells where needed.
- Added depth testing to composite coarse and fine passes correctly.
- Later extended the split renderer to carry optional diagnostic channels:
  velocity, suspended sediment, and signed terrain change.
- Changed water back from translucent to opaque, with shallow water rendered
  lighter blue and deeper water darker blue.
- Added renderer display modes for:
  - water depth
  - flow speed
  - suspended sediment
  - erosion/deposition
- Kept water side masking so adjacent wet-cell side faces are hidden and pools
  do not show distracting internal blue planes.

### Virtual Pipe And O'Brien-Style Flow

- Read and discussed the O'Brien/Hodgins dynamic simulation paper that inspired
  a volume-flow experiment.
- Built a virtual-pipe fluid experiment with eight-neighbor flow and optional
  tunnel/pipe visualization.
- Removed splashing from scope and focused on flow behavior.
- Changed this experiment to use one-foot water columns rather than one-inch
  columns after the first version was too slow.
- Fixed scale side effects where terrain heights and water depths were being
  interpreted at the wrong resolution.
- Applied the one-foot coarse simulation idea to the shallow-water simulator as
  well.
- Discussed combining shallow-water surface behavior with pipe-flow transport.

### Stability And Missing Physics

- Compared artificial stabilizers against physical stabilizers.
- Identified that friction/roughness was the meaningful missing physics for
  stable water behavior, rather than simply lowering time step, pipe area, or
  damping values.
- Added Manning-style rough-bed friction in the terrain-head pipe experiment.
- Added steady-state detection so the simulation can stop once water motion is
  near enough to stable, then wake again when new water is added.

### Hydraulic Erosion Research

- Discussed which fluid/erosion approaches make sense for real-time games.
- Created a Perplexity research prompt for erosion and fluid-flow simulation
  sources.
- Reviewed the Stava 2008 and FastErosion PG07 papers after realizing the
  initial Dorian hydraulic erosion attempt had been built without those sources.
- Reverted direction from the first Dorian erosion sim and rebuilt around the
  FastErosion-style loop.

### Hydraulic Erosion + Rainfall

- Added a purpose-built erosion seed map with an incline and valleys so erosion
  behavior is easier to see than on the default shared grass field.
- Created the CPU 11 `Hydraulic Erosion + Rainfall` experiment.
- Used the FastErosion PG07 baseline loop:
  1. water increment
  2. virtual-pipe flux
  3. water height and velocity update
  4. sediment capacity
  5. erosion/deposition
  6. sediment transport
  7. evaporation
- Preserved the selected-cell water source.
- Changed rain from "add an inch everywhere" into visible falling drops that
  land on water/terrain cells and then join the normal flow and erosion loop.
- Fixed runaway deep-pit behavior by limiting per-step terrain movement.
- Added observability:
  - phase labels
  - `Step Next Phase`
  - sediment capacity slider
  - dissolving slider
  - deposition slider
  - max terrain step slider
  - velocity/sediment/terrain-change renderer channels

## Validation Performed

- Ran the required Direct3D 12 knowledge-base preflight lookups before touching
  D3D12/root-signature/shader paths.
- Rebuilt Grass Field 003 successfully after the latest renderer and simulator
  changes:

```powershell
cmake --build build --target grannys_house_trials_grass_field_003
```

- The build recompiled the split LOD HLSL vertex and pixel shaders and produced
  the Grass Field 003 executable successfully.

## Current State

- Grass Field 003 is now a richer experiment workbench rather than a straight
  lesson sequence.
- The split renderer is the main practical renderer for the fluid/erosion
  experiments because it keeps water readable and exposes diagnostics.
- The FastErosion-inspired hydraulic erosion experiment is the current most
  useful path for erosion work.
- The simulator still runs on CPU and is intentionally small-scale.
- A full one-inch erosion grid plus one-inch water grid over the same footprint
  would jump from `100 x 100` cells to about `1200 x 1200` cells, or roughly
  `144x` more cells before extra solver/rendering costs. That should be a
  separate, deliberately bounded experiment.

## Notes For Future Work

- Try a tiny or tiled one-inch erosion/water experiment before scaling it to the
  full `100 x 100` foot field.
- Consider a GPU version of the FastErosion loop once the CPU behavior feels
  educationally correct.
- Keep adding sequence diagrams to new lessons; they are especially useful for
  explaining multi-pass simulations.
- If water looks too flat, prefer color ramps and diagnostic modes before
  returning to translucency. Translucency made the water less readable here.
- Keep paper-derived experiments honest: if a parameter is not part of the
  paper model, name it clearly as an implementation/debug helper or leave it
  out of the strict recreation.
