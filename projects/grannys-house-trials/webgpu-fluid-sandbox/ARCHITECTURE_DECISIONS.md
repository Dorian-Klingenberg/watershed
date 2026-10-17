# WebGPU Fluid Sandbox — Architecture Decisions

This is the decision ledger for the browser-based fluid simulation workbench. It
records the planning decisions and the initial implementation choices. The
project lives in this directory as a clean, independent application with no code
dependencies on the existing Granny's House Trials work. Existing experiments
are references for behavior and lessons only.

## Decision Ledger

### ADR-001: Build a browser fluid-simulation workbench

- **Status:** Accepted direction
- **Decision:** Explore a browser-based sandbox for Granny's garden fluid
  simulations.
- **Reason:** The user wants a browser platform relevant to their clients'
  Blazor experience and a place to grow fluid simulations over time.
- **Scope guard:** Begin with the cellular-fluid experiment. More advanced fluid
  simulations can come later.

### ADR-002: Use Blazor WebAssembly for the application shell

- **Status:** Accepted direction
- **Decision:** Use Blazor WebAssembly, with Fluent UI Blazor controls arranged
  around the WebGPU visualization pane.
- **Constraints:** Keep browser JavaScript interop as limited as practical. Dear
  ImGui is not the intended UI.
- **Implementation:** Fluent UI buttons sit above side-by-side CPU and GPU field
  views.

### ADR-003: Keep the project independent of prior implementations

- **Status:** Accepted direction
- **Decision:** The new project should live in a clean, separate location and
  have no code dependencies on prior work.
- **Clarification:** Existing code, renderer behavior, shaders, and measurements
  may be studied and recreated; they are references, not dependencies.
- **Implementation:** The approved implementation lives in this directory. The
  name remains provisional.

### ADR-004: Isolate browser GPU concerns from domain concepts

- **Status:** Accepted and implemented
- **Decision:** Treat the GPU API and browser GPU resource management as a
  platform boundary, not as the home for domain concepts.
- **Direction:** Domain concepts should not become trapped in JavaScript.
  Blazor/C# is the expected place for application and domain-facing code; a
  browser-side module is the expected place for WebGPU interactions.
- **Interop constraint:** Prefer a small number of coarse operations over
  per-cell or per-frame interop.
- **Implementation:** C# owns the domain-facing model, CPU reference, actions,
  statistics, and comparison. A coarse JavaScript interop module owns WebGPU
  resources and dispatch. WGSL contains the numeric operations required by the
  GPU kernel, not application-domain concepts.

### ADR-005: Start by recreating the cellular-fluid experiment

- **Status:** Accepted direction
- **Decision:** Recreate the existing cellular-fluid experiment before trying
  more advanced fluid models.
- **Scope:** Focus on the fluid simulation itself and only the minimum useful
  interactivity for a first platform iteration.
- **Controls:** Reset, pour water at the center, step both implementations once,
  and request a CPU/GPU comparison.
- **Boundary behavior:** Drain water from the outer grid cells after each tick,
  matching the initial experiment's open-edge behavior.
- **Reference:** Include the C# implementation in the first slice. Compare every
  cell with a `0.0001` absolute tolerance on explicit request.
- **Deferred:** MAC-grid and other advanced fluid simulations.

### ADR-006: Revisit the existing renderer before deciding the WebGPU rendering path

- **Status:** Review finding; first-iteration choice implemented
- **Finding:** The existing renderer is unconventional. It draws a full-screen
  triangle, reconstructs a camera ray per pixel, and uses a pixel shader to
  traverse a grid of variable-height columns with DDA. It does not first build a
  conventional terrain mesh.
- **Fluid representation:** Terrain height and fractional water depth are
  separate. Rendering derives the water surface from both and shades wet
  columns using water depth.
- **GPU precedent:** The existing Phase 1 compute experiment reads results back
  for the existing renderer. Phase 2 keeps water GPU-resident and renders from
  GPU terrain and water buffers, using snapshots for inspection or edits.
- **Implementation:** Use a regular 2D cell map for the first visible iteration.
  The GPU field is read back for drawing after a manual step; raycast rendering
  is deferred.

### ADR-007: Browser target and success criteria

- **Status:** Initial direction implemented; broader criteria remain open
- **Decision:** Chrome is acceptable as the initial browser target.
- **Success criteria:** Intentionally undefined for now. The user wants to see
  an early iteration before deciding what visual and interaction qualities are
  right.

## Implemented architecture

The following is the current implementation, not a finalized performance
architecture:

- `Simulation/CellularFluidSimulation.cs` owns the C# terrain, water state,
  cellular-flow reference, point pours, barrier height policy, reset, and summary.
- `Pages/Home.razor` serializes paired ticks and edits, owns Run/Pause, and
  displays selected-cell information. Camera navigation remains usable while paused.
- `wwwroot/js/landscape-view.js` owns projected height-column drawing, terrain
  picking, linked camera/navigation, and gesture previews. Committed cell edits
  are dispatched to C#; it does not own fluid or terrain-edit rules.
- `wwwroot/js/fluid-gpu.js` owns WebGPU resources and executes the WGSL
  propose/apply passes. The GPU state stays in ping-pong buffers between actions.
- Reset uploads the complete starting field. Pours send additive edits, preserving
  each active implementation's independent water state. Barrier strokes update terrain
  without replacing water. Each display batch reads GPU water back once for drawing.
- After each tick, water on the field's outer boundary is drained.
- `Compare states` requests the cached completed GPU snapshot across interop and compares it to the
  C# snapshot, reporting mismatch count, maximum cell error, and water-total
  error.
- When WebGPU cannot initialize, the C# reference remains available. GPU failure
  restores the last completed display baseline/tick before activating CPU fallback;
  an unrecoverable baseline explicitly resets, never resumes stale reference water.
- The CPU reference toggle skips solver stepping, water snapshots, drawing, and
  summary scans while off. C# retains domain/terrain/edit policy. Re-enabling uses
  a fresh GPU readback and its tick as an explicitly announced new baseline;
  subsequent edits/steps remain independent, not ongoing synchronization.
- Both views share selection, hover, camera, and two-axis landscape navigation.
  The depth-axis slider sits physically to the right of the views. Vertical
  page scrolling and responsive layout keep controls reachable on small screens.
- The sloped scene and barriers are reusable across future solver experiments;
  momentum-based waves remain outside the current cellular solver's scope.
- `tests/FluidModelChecks` exercises conservation, flow, repeatability, boundary
  pours, input validation, and comparison behavior.
- `.github/workflows/deploy-webgpu-fluid-sandbox.yml` publishes the static app to
  GitHub Pages.
- A serialized display-paced loop replaces the fixed tick delay. Configurable
  1/2/4/8-tick batches amortize one GPU readback; manual Step remains one tick.
  CPU delta storage, unchanged terrain, projected geometry, and column order are
  reused. Only dirty, visible, enabled canvases redraw.
- Actual redraw FPS, ticks/sec, draw cost, CPU step cost, and GPU dispatch/readback
  wall-clock roundtrip are sampled every 500 ms. The latter is not GPU execution
  time. Selection reads cached display depths without another GPU map or full
  snapshot transfer to C#.
- Dedicated message-passing workers are feasible on static hosting without shared
  memory; moving the C# runtime or WebGPU device into one needs a separate ownership
  and bootstrap design. Shared-memory/WASM threads require cross-origin isolation,
  which this Pages workflow does not configure. No worker or parallel mixed-solver
  architecture is required here; see README for the hosting assessment.

The earlier diagrams below are conceptual planning sketches; the current
implementation boundary is described above and in [README.md](./README.md).

## Historical architecture sketches

These original planning sketches are retained for decision history. They are
not the implementation contract; see the preceding implementation summary and
the project README for the current architecture.

### Interaction sequence: Blazor/C# and browser GPU boundary

```mermaid
sequenceDiagram
    actor User
    participant UI as Fluent UI Blazor
    participant App as Blazor app / C#
    participant Domain as Fluid model / CPU reference
    participant Bridge as JS interop facade
    participant WebGPU as Browser GPU backend
    participant GPU as WebGPU device and queues
    participant Shader as WGSL compute and render shaders
    participant Canvas as Visualization canvas

    User->>UI: Choose reset, add water, or step
    UI->>App: Raise user action
    App->>Domain: Apply domain-level action or update reference
    App->>Bridge: Invoke coarse operation (proposed)
    Bridge->>WebGPU: Forward configuration/action
    WebGPU->>GPU: Upload edits or dispatch requested work
    GPU->>Shader: Run cellular-fluid compute
    Shader->>GPU: Write next water state
    opt GPU-resident rendering selected
        GPU->>Shader: Run raycast render pass
        Shader->>Canvas: Write visualization
    end
    opt Snapshot or CPU/GPU comparison requested
        GPU-->>WebGPU: Read back selected or full state
        WebGPU-->>Bridge: Return snapshot
        Bridge-->>App: Resolve interop call
        App->>Domain: Compare with reference (policy open)
        App-->>UI: Update summary or comparison
    end
    opt Simpler first-iteration display selected
        WebGPU-->>Bridge: Return display data (transfer shape open)
        Bridge-->>App: Resolve interop call
        App-->>UI: Present temporary visualization (open)
    end
```

The detailed call granularity and state ownership in this historical sketch are
superseded by the implemented architecture above.

### Main component class diagram

```mermaid
classDiagram
    class FluentUIComponents {
        +reset()
        +addWater()
        +step()
        +showStatus()
    }
    class BlazorApplication {
        +handleUserAction()
        +initialize()
        +requestSnapshot()
    }
    class FluidModel {
        +reset(seed)
        +addWater(action)
        +step()
        +getSnapshot()
    }
    class CpuReference {
        +step()
        +getSnapshot()
    }
    class InteropFacade {
        +initialize(canvas, config)
        +applyAction(action)
        +step(count)
        +snapshot()
        +dispose()
    }
    class WebGpuBackend {
        +requestAdapterAndDevice()
        +createResources()
        +dispatchFluidSteps()
        +render()
        +readSnapshot()
        +dispose()
    }
    class GpuState {
        +terrainBuffer
        +waterBufferA
        +waterBufferB
        +proposalBuffer
    }
    class WgslCompute {
        +proposeFlows()
        +applyFlows()
    }
    class WgslRaycastRenderer {
        +drawFullscreenTriangle()
        +traceColumnGrid()
        +shadeWaterAndTerrain()
    }
    class Canvas {
        +present()
    }

    FluentUIComponents --> BlazorApplication : user actions / status
    BlazorApplication --> FluidModel : domain actions
    FluidModel --> CpuReference : reference implementation (proposed)
    BlazorApplication ..> InteropFacade : coarse JS interop (proposed)
    InteropFacade --> WebGpuBackend : browser module calls
    WebGpuBackend --> GpuState : owns resources
    WebGpuBackend --> WgslCompute : dispatches
    WebGpuBackend --> WgslRaycastRenderer : optional rendering path
    WgslCompute --> GpuState : reads / writes
    WgslRaycastRenderer --> GpuState : reads terrain / water
    WgslRaycastRenderer --> Canvas : renders
    WebGpuBackend --> Canvas : configures presentation
```

The component names and operations in this historical sketch are conceptual.
The current browser adapter is plain JavaScript; WGSL is used for WebGPU compute.

## References Reviewed

These existing files informed the rendering and simulation review. The proposed
browser project does not depend on them:

- `projects/grannys-house-trials/subprojects/grass-field-003/shaders/grass_field_renderer.hlsl`
- `projects/grannys-house-trials/subprojects/grass-field-003/shaders/fluid_compute_phase1.hlsl`
- `projects/grannys-house-trials/subprojects/grass-field-003/lesson_experiment_cellular_fluid_sim.md`
- `projects/grannys-house-trials/subprojects/grass-field-003/lesson_experiment_cellular_fluid_hlsl_compute_phase1.md`
- `projects/grannys-house-trials/subprojects/grass-field-003/lesson_experiment_cellular_fluid_hlsl_compute_phase2_tiled_resident.md`
- `projects/grannys-house-trials/subprojects/grass-field-003/LESSON_CATALOG.md`
