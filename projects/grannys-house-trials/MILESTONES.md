# Milestones

## Purpose

This document breaks `Granny's House Trials` into implementable slices.

The main rule is:

> every milestone should move us toward one complete, legible, replayable
> in-world testing round

## Milestone 1: Project Scaffold

Deliverables:

- project folder
- core docs
- module structure
- one project test target
- scope boundaries
- first scenario choice
- first cast / scoring model

Success condition:

- the project has a stable identity in the repo

## Milestone 2: First Playable Space

Deliverables:

- D3D12 app shell
- camera and movement
- tiny Granny's Yard space
- basic voxel terrain / structure representation

Success condition:

- we can move around a readable homestead yard

Current note:

- `subprojects/grass-field-001` is already the terrain-and-camera proving slice for this milestone
- it should stay a thin visual harness around shared `sim` and `gfx` code
- its current renderer now uses shader-side column raycast rendering
- it is no longer dependent on CPU-emitted cube meshes for the field view
- a full general voxel traversal renderer is still a near-term goal, but not
  done yet

## Milestone 3: First Testable Mechanic

Deliverables:

- simple water-routing or drainage interaction
- visible success / failure states
- one hidden dependency
- scenario reset

Success condition:

- the yard can produce at least two meaningfully different outcomes from
  different interventions

Status:

- complete

Current note:

- Milestone 3 is now satisfied by the dedicated `playtest::GrannysYardSession`
  plus the host-shell controls in `grass-field-001`
- keep the round behavior stable and reusable rather than expanding the
  renderer further

## Milestone 4: Evidence Board

Deliverables:

- tracked round accomplishments
- tracked incidents
- tracked discoveries
- round summary UI
- reusable playtest-facing evidence projection

Success condition:

- a round leaves behind enough evidence to judge what happened

Implementation note:

- keep the shared tester-facing packet and evidence surface in a dedicated `playtest` module
- keep world legality and hidden-structure truth in `sim`
- do not create a driver subproject yet
- the runnable now has a dedicated `playtest::EvidenceBoardPanel` child window for the board view

Current note:

- Milestone 4 is complete for the current thin-shell runnable; the next UI upgrade can revisit presentation polish without reopening the evidence-board milestone

## Milestone 5: Cast Layer

Deliverables:

- Builder role framing
- Chaos Tester role framing
- Systems Auditor role framing
- placeholder reactions or dialogue hooks

Success condition:

- the round already feels different depending on which tester is acting or
  speaking

## Milestone 6: Host-Judged Competition Loop

Deliverables:

- end-of-round scoring screen
- manual point assignment workflow
- winner declaration
- reset into next round

Success condition:

- one full round can be played, reviewed, judged, and restarted

## Milestone 7: First Watchable Pass

Deliverables:

- clearer presentation
- better readability of outcomes
- first round that is understandable to a viewer

Success condition:

- someone unfamiliar with the project can watch one round and understand the
  goal, the failure, and the ruling

## Current Recommendation

Move into:

- Milestone 4 evidence-board presentation
- clearer round summaries
- better host-facing evidence readability

Do not move into:

- larger maps
- multiple settlements
- advanced AI autonomy
- broad inventory systems
- deep narrative scripting
- driver applications under `subprojects/`

until Milestone 6 is working.

---

## Infrastructure Support Tasks

### D3D12 Renderer Module Extraction

This is not a milestone, but a supporting infrastructure task that enables faster iteration and cleaner architecture.

**Goal**: Extract the D3D12 rendering infrastructure from grass-field-001 into a reusable, RAII-based shared module.

**Why now**:
- grass-field-001 and grass-field-002 both contain duplicate D3D12 device management code
- Each new subproject replicates complex device/swap-chain/frame-sync boilerplate
- A shared component reduces ~600 LOC of copy-paste code per subproject
- Extraction supports the principle: "Keep source grouped by domain"

**Scope**:
- Extract, don't refactor: Take existing proven code from grass-field-001
- Module focus: Device management, frame sync, GPU buffers, pipeline creation
- No simulation or domain logic included
- Full RAII and exception safety semantics

**Relationship to milestones**:
- Supports **Milestone 2** (renderer stability and reusability)
- Reduces technical debt in **M3+** runnables
- Required before scaling to multiple subprojects
- Does **not** block any active milestone

**For more details**: See [STATUS.md](STATUS.md#d3d12-renderer-module-extraction) and [modules/gfx/d3d12_renderer/ARCHITECTURE.md](./modules/gfx/d3d12_renderer/ARCHITECTURE.md)
