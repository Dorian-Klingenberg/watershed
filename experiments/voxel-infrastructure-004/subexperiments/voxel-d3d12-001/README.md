# Voxel D3D12 001

This sub-experiment is a first Direct3D 12 visualization for the voxel world defined in the parent experiment.

It currently does one small thing:

* renders the shared voxel world as colored cubes
* uses the same world definition as the console experiment
* supports mouse rotation around the scene

## Controls

* left mouse drag: orbit camera
* `Esc`: quit

## Scope

This is intentionally a very small graphics slice.
It is not yet:

* a full engine
* chunked
* instanced
* optimized for large worlds
* showing hidden voxels

It is just the first D3D12 viewer for the current voxel definition.
## Voxel D3D12 001

This sub-experiment is a first graphics-side follow-up to the terminal viewer in [BEGINNERS_GUIDE.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/BEGINNERS_GUIDE.md).

Its job is intentionally narrow:

* render the current shared Experiment 004 world definition in Direct3D 12
* keep the same world-space convention
* let you orbit around the voxel mass with the mouse

## Current Scope

Right now this is a visualization sub-experiment, not a full new simulation.

It reuses the shared world definition from:

* [world_definition.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.h)
* [world_definition.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.cpp)

That means it should match the same toy world the console dashboard is inspecting:

* `+Y` is world up
* `XZ` is the horizontal ground plane
* lower `y` means deeper underground

Only currently visible, non-hidden voxels are turned into geometry.
This keeps the first pass simple and makes the scene correspond more closely to the readable view, not the concealed internals.

The scene now uses a simple directional light with ambient fill so the voxel mass reads as volume instead of flat colored triangles.

## Controls

* left mouse drag: orbit camera
* mouse wheel: zoom
* `Esc`: quit

## Build Target

The CMake target name is:

* `voxel_d3d12_001`

It is added from:

* [experiments/voxel-infrastructure-004/CMakeLists.txt](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/CMakeLists.txt)

## Notes

This viewer is deliberately crude.
It is a stepping stone toward a proper GPU-side voxel working set, not that system itself.

If this sub-experiment evolves, this README should be updated so it continues to describe what is actually being rendered and what inputs are supported.
