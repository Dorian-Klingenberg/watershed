# Current Status

This document tracks where `Granny's House Trials` actually stands right now.

Its job is to separate:

- completed work
- partially implemented work
- planned but not yet integrated work

This should be kept current as the project evolves.

## Canonical Mission

**Mission:** prove that a small, agent-friendly yard scenario can reveal
hidden infrastructure, record meaningful evidence, and support a real
drainage/repair round without losing the larger world-first identity.

Current architectural boundary:

- the round logic now lives behind `playtest::GrannysYardSession`
- `main.cpp` is the host shell and viewport presentation layer
- the session owns the drainage round state, legal actions, evidence, and
  packet projection
- the runnable now has a dedicated evidence-board child window fed from the
  shared playtest evidence view

Machine-readable context:

- [AGENT_CONTEXT.json](/D:/Repos/Games/TheGame/projects/grannys-house-trials/AGENT_CONTEXT.json)

## Current Phase

The project is currently:

> past the basic scaffold stage, strong on terrain/renderer proving work, and
> still early on the actual "test cast competition round" loop

In plain terms:

- the terrain and rendering proving slice is real and fairly advanced
- the shared scenario and playtest protocol types are now partially wired into
  the runnable
- the first full in-world testing round is still incomplete, but the first
  real drainage mechanic loop now exists in the app
- the drainage round has been moved behind a dedicated session component so
  it can be reused by future tools and driver apps

## Milestone Status

### Milestone 1: Project Scaffold

Status: `complete`

What is done:

- project docs
- shared folder structure
- single shared project library
- one Catch2 test executable
- basic scope and identity lock

### Milestone 2: First Playable Space

Status: `complete`

What is done:

- D3D12 app shell
- camera and viewport interaction
- inspectable terrain field
- shared sim-backed terrain data
- lighting, shadows, AO, and bounce-light approximation
- coarse / refined / hybrid display modes

Primary runnable:

- [grass-field-001/README.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-001/README.md)

### Milestone 3: First Testable Mechanic

Status: `complete`

Current focus:

- keep the drainage round stable and inspectable
- avoid reopening renderer scope unless it directly helps the mechanic

What is done:

- gravity erosion stepping on the `1-inch` refinement layer
- sparse refined patch ownership model
- Granny's Yard scenario types in `sim`
- a deterministic shared yard drainage mechanic with:
  - one objective
  - one hidden dependency
  - visible garden / cellar / path consequences
  - target-aware legal actions
  - resettable round state
- runnable UI controls for target actions, round advance, and round reset
- a field-size selector for square preset sizes that resets the field and
  scenario together
- agent packet output that now includes objective, focused target, legal
  actions, recent evidence, compact tokenized recent events that keep only
  the first no-op activity until the yard state changes, and success/failure
  flags
- dedicated `playtest::GrannysYardSession` component to keep round orchestration
  out of the host shell

What is still intentionally deferred:

- richer evidence-board presentation
- host scoring / winner judgment
- a broader action vocabulary beyond the current route / dig / pack /
  inspect loop

### Milestone 4: Evidence Board

Status: `complete`

What is done:

- round-log and evidence-related sim/playtest types
- tested protocol and evidence-board projections in shared code
- host UI text now surfaces recent scenario events and evidence counts
- evidence board is visible in the main runnable
- end-of-round summary flow freezes the board into terminal states
- cleaner board layout and summary styling are in place for the current shell

What remains intentionally deferred:

- richer evidence-board componentization for a future UI upgrade

### Milestone 5: Cast Layer

Status: `partial`

What is done:

- cast roles and competition rules are documented
- tester-role and protocol types exist in `playtest`

What is not done:

- cast behavior in the runnable
- round-to-round tester voice/presentation

### Milestone 6: Host-Judged Competition Loop

Status: `not started`

---

## Technical Infrastructure Tasks

### D3D12 Renderer Module Extraction

Status: `Phase 1 complete` (Architecture & documentation)

**Purpose**: Extract grass-field-001's Direct3D12 rendering pipeline into a production-grade shared component with full RAII semantics, exception safety, and Windows integration.

**Documentation**:
- [modules/gfx/d3d12_renderer/ARCHITECTURE.md](./modules/gfx/d3d12_renderer/ARCHITECTURE.md) - Complete design and API reference
- [modules/gfx/d3d12_renderer/README.md](./modules/gfx/d3d12_renderer/README.md) - Quick reference

**Rationale**:
- grass-field-001 and grass-field-002 both duplicate D3D12 device/swap chain/frame management code
- Extraction follows project principle: "Keep source grouped by domain, not by speculative build architecture"
- RAII-based design ensures robust cleanup and exception safety across all consumers
- Shared component enables faster iteration on new subprojects

**Phase breakdown**:

**Phase 1 (Complete):**
  - ✓ Architecture document and design review
  - ✓ CMake integration planning
  - ✓ Core class interfaces designed

**Phase 2 (Planned):**
  - Header declarations for `D3D12Context`, `GraphicsFrame`, `DeviceResources`, `PipelineBuilder`
  - Implementation of core classes (~1000 LOC)
  - Unit tests with Catch2 (~500 LOC)
  - Timeline: 8-12 hours

**Phase 3 (Planned):**
  - Refactor grass-field-001 to use module
  - Equivalence validation (pixel output matching)
  - Remove ~600 LOC of inline boilerplate
  - Timeline: 4-6 hours

**Phase 4 (Future):**
  - grass-field-002 adoption
  - New subproject integration
  - Performance profiling hooks

**Impact on milestones**:
- Supports Milestone 2 (renderer stability and reuse)
- Reduces technical debt in M3+ runnables
- No impact on M3 completion, only on iteration speed for future subprojects

Not yet built:

- manual point-award flow
- winner declaration
- round reset into judged replay loop

### Milestone 7: First Watchable Pass

Status: `not started`

Not yet built:

- a full viewer-facing round presentation
- understandable episode-like flow from objective to ruling

## Built And Working Today

- shared `sim`, `playtest`, `gfx`, and `util` code compiled as one project library
- one active D3D12 runnable:
  [main.cpp](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-001/main.cpp)
- additional authored-map runnable:
  [grass-field-004/main.cpp](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-004/main.cpp)
- build-time shader compilation with separate coarse / refined / hybrid PSOs
- shader-side column raycast renderer
- coarse `1-foot` field plus `1-inch` refinement concepts for erosion/detail
- adaptive ownership model for coarse vs refined terrain volume
- click inspection and AI-agent JSON snapshot export
- passing unit tests across sim, playtest, gfx, and util

### 2026-05-23 Grass Field 004 Update

Status: `implemented and verified`

What is now done:

- `grass-field-004` integrated as a dedicated authored visual map slice
- `108 x 156` foot authored footprint expanded to `1296 x 1872` one-inch cells
- selective inch-detail for structures/features with broad foot-quantized ground
- material-id propagation through CPU buffers and all renderer shader paths
- material color coding and inspector material labels
- live hover highlight for pending column selection
- control mapping update:
  - left-click select
  - middle-drag orbit
  - right-drag pan
  - scroll zoom
- window resize support with swapchain/depth/RTV recreation
- picking upgraded from ground-plane projection to DDA + per-column AABB tests
- right-drag pan upgraded to world-plane delta anchored at selected column height

What remains intentionally deferred in 004:

- scenario action anchors and round mechanics
- host scoring/evidence flow for this authored map slice

## Intentionally Not Done Yet

- full arbitrary voxel ray marcher / cube marcher
- general volumetric mixed-resolution traversal beyond the current column-field approach
- water-routing round mechanic in the runnable
- evidence-board UI
- host scoring screen
- tester-cast-driven round presentation

## Main Risk Right Now

The project has advanced faster in renderer and terrain research than in the
actual "funny, useful testing round" loop.

That is not wasted work, but it does mean the next best progress is probably
not more renderer novelty by default.

## Recommended Next Focus

The strongest next step is:

> move into Milestone 4 by making the evidence board and round summary more
> intentional, while keeping the session boundary stable

In practical terms, that likely means:

- improve the readability and feedback of the existing water-routing round
- surface evidence in a more deliberate board-style presentation
- keep extracting host-shell responsibilities out of `main.cpp` when they
  clearly belong to the session or shared playtest layer
