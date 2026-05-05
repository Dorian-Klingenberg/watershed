# Development Guide

This project is intentionally scaffolded for readability, testability, and
simple decisions.

## Main Rules

- Keep source grouped by domain, not by speculative build architecture.
- Keep headers small and stable.
- Put implementation in `.cpp` files whenever practical.
- Push platform code to the edge.
- Test pure logic first.
- Point back to existing repo canon instead of duplicating world assumptions in
  every local doc.
- Do not invent framework layers before repeated pain makes them necessary.

## Current Structure

- `modules/util`: shared low-level value types and helpers
- `modules/sim`: world truth, scenario state, evidence facts, and shared
  terrain representations
- `modules/playtest`: tester-facing turn packets, transcripts, and
  evidence-board projections
- `modules/gfx`: render-adjacent but platform-agnostic types (camera, constants)
  - `modules/gfx/d3d12_renderer`: RAII-based Direct3D12 device/buffer/pipeline management (Phase 1: architecture only)
- `subprojects/`: focused runnable slices that reuse the shared project code
- `tests/`: one Catch2 test executable with all current unit tests

## Reuse Existing Concepts

Before adding a new project-level type or document, check whether the concept
already exists in either the shared repo docs or one of the local modules.

Examples that already exist here:

- world-first consequence framing in [docs/game_vision.md](/D:/Repos/Games/TheGame/docs/game_vision.md)
- passive fluidic infrastructure assumptions in [docs/ancient_technology.md](/D:/Repos/Games/TheGame/docs/ancient_technology.md)
- Granny's Yard scenario state in `sim`
- turn packets and evidence-board projections in `playtest`
- orbit camera behavior in `gfx`

Prefer reusing and extending those before adding parallel versions.

## Current Build Shape

The code is still organized in `util`, `sim`, `playtest`, and `gfx` folders,
but those folders no longer compile as separate subsystem libraries.

Right now the build stays intentionally flat:

- one shared project library: `grannys_house_trials::core`
- one active runnable target: `grannys_house_trials_grass_field_001`
- one Catch2 test executable: `grannys_house_trials_tests`

That matches the current size of the project better and avoids locking us into
more compiled boundaries than the code has earned.

## Current Test Approach

This project now uses Catch2 for unit tests.

Why Catch2 for this scaffold:

- readable test syntax
- individual test discovery through CMake and CTest
- filtering by test name or tag
- colored output and standard reporting

We are still keeping the test layout simple:

- one test executable for the whole project
- small focused test cases
- no extra wrapper framework on top of Catch2

## Current Targets

Shared project library:

- `grannys_house_trials::core`

Runnable target:

- `grannys_house_trials_grass_field_001`

Test executable:

- `grannys_house_trials_tests`

Per-test selection is provided by Catch2 discovery, so individual `TEST_CASE`s
show up as separate CTest tests.

## Useful Build Habits

Build the one target you are working on when possible.

Examples:

- build the runnable
- build the tests
- run the tests

This is usually better than rebuilding everything after every small change.

## Example Commands

Configure:

```powershell
cmake -S . -B build
```

Build one target:

```powershell
cmake --build build --target grannys_house_trials_tests
```

Build the first DX12 subproject:

```powershell
cmake --build build --target grannys_house_trials_grass_field_001
```

Run all project tests:

```powershell
ctest --test-dir build -R grannys_house_trials --output-on-failure
```

Run one discovered test case by name:

```powershell
ctest --test-dir build -R "RoundLog counts evidence by type" --output-on-failure
```

Run one test executable directly with Catch2 tags:

```powershell
.\build\projects\grannys-house-trials\tests\grannys_house_trials_tests.exe [round_log]
```

## When To Add More Structure

Only add a new abstraction when one of these becomes true:

- we are repeating code in multiple places
- a compile unit is getting too large
- a dependency boundary is getting muddy
- tests are becoming hard to write
- build times are consistently becoming annoying for a real reason

If none of those are happening, prefer simpler code.

## Intentional Omissions

The full game renderer still does not exist, and that is still deliberate.

What does now exist is one tiny graphics-side proving slice:

- `subprojects/grass-field-001`

Its job is to prove the shared build structure can support a real D3D12 window
without forcing the entire project into a renderer-first architecture.

Current renderer status:

- `grass-field-001` now renders through a shader-side column raycast that
  still reuses `sim::GrassField`
- it is not yet a complete arbitrary voxel ray marcher or cube marcher
- future rendering slices can build outward from this path instead of returning
  to CPU mesh emission

## Graphics/Rendering Patterns

### Renderer Parity Rule (001 and 002)

To avoid accidental renderer forks:

- Treat `grass-field-001` as the canonical world-render implementation.
- Treat `grass-field-002` as a UI/session shell that consumes the same world
  render path.
- Do not add a 002-only world pipeline, standalone world shaders, or
  substitute geometry pass.
- Implement world-render changes in shared/canonical code first, then wire
  both subprojects to it.

Required validation for renderer changes:

1. Build both `grannys_house_trials_grass_field_001` and
   `grannys_house_trials_grass_field_002`.
2. Launch both and verify world visuals match for equivalent scenario state.
3. Only then update docs/status and mark the renderer task complete.

### The D3D12 Renderer Module

New subprojects that need Direct3D12 rendering should use the shared `modules/gfx/d3d12_renderer/` module rather than copying device/frame management code.

**When to use**:
- You need a D3D12 rendering context in a new subproject
- You want RAII-based device management with exception safety
- You need GPU buffer allocation, pipeline state creation, or frame synchronization

**How to use**:

See [modules/gfx/d3d12_renderer/ARCHITECTURE.md](modules/gfx/d3d12_renderer/ARCHITECTURE.md) for complete API reference and examples.

**What the module provides**:
- `D3D12Context`: Device, swap chain, frame management (RAII)
- `GraphicsFrame`: Per-frame command list and state transitions
- `DeviceResources` and `GPUBuffer<T>`: Typed GPU buffer allocation
- `PipelineBuilder`: Shader loading and PSO caching

**What you provide**:
- HWND for rendering
- Scene constants and data marshaling
- Shader files (pre-compiled `.cso` blobs)
- Render loop orchestration

**Example**:
```cpp
// Create context
D3D12Context graphics(hwnd, 1280, 720);

// Create pipeline
PipelineBuilder builder;
auto pso = builder.build_pipeline(graphics.device(), "my_pipeline", {...});

// Main loop
while (running) {
    GraphicsFrame frame(&graphics, graphics.current_frame_index());
    frame.begin();
    // ... record GPU commands ...
    frame.end();
    frame.execute();
    graphics.present();
}
```

**Current status**: Module is in Phase 1 (architecture complete, implementation beginning). See [STATUS.md](STATUS.md#d3d12-renderer-module-extraction) for timeline.
