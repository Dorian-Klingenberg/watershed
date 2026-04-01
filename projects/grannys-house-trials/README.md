# Granny's House Trials

`Granny's House Trials` is a vertical-slice prototype project for the ancient
infrastructure game.

Its job is to prove one combined idea:

> a small in-world testing session can be both genuinely useful for
> development and entertaining because it is framed as a ridiculous
> competition between recurring tester personalities

## Canonical Mission

**Mission:** prove that a small, agent-friendly yard scenario can reveal
hidden infrastructure, record meaningful evidence, and support a real
drainage/repair round without losing the larger world-first identity.

Machine-readable context:

- [AGENT_CONTEXT.json](/D:/Repos/Games/TheGame/projects/grannys-house-trials/AGENT_CONTEXT.json)

Keep the prose docs human-friendly and keep `AGENT_CONTEXT.json` as the
compact ingest target for agents.

This is not the full game.
This is not a throwaway experiment.
It is a focused project intended to discover whether the "testing as show
format" concept belongs in the larger game development process and possibly
in the eventual audience-facing format.

Current architectural step:

- the Granny's Yard round is now owned by a dedicated
  `playtest::GrannysYardSession`
- `main.cpp` should stay focused on the host shell, viewport, and UI plumbing
- the session owns the round state, legal actions, evidence projection, and
  turn packet generation
- the compact packet keeps tokenized recent events, but repeated no-op
  activity is only logged once until the yard actually changes
- the runnable now includes a dedicated evidence-board child window that
  reads the playtest evidence view
- Milestone 3 is the working drainage round now, so the next focus is
  evidence-board polish rather than more renderer architecture

## Relationship To The Broader Repo

This project inherits its world and systems assumptions from the shared repo
docs.

Use these as the broader canon:

- [docs/game_vision.md](/D:/Repos/Games/TheGame/docs/game_vision.md)
- [docs/core_loops.md](/D:/Repos/Games/TheGame/docs/core_loops.md)
- [docs/ancient_technology.md](/D:/Repos/Games/TheGame/docs/ancient_technology.md)
- [docs/simulation_model.md](/D:/Repos/Games/TheGame/docs/simulation_model.md)

Use the `grannys-house-trials` docs for slice-specific decisions:

- the testing-as-show format
- the Granny's Yard starting scenario
- the host-judged competition loop
- the shared module boundaries for this project line

## Core Principles

- The world matters more than the joke.
- The competition exists to reveal useful truths about systems.
- The first location should stay small, cozy, and failure-prone.
- Ancient infrastructure should remain passive, fluidic, and partly hidden.
- The system should track evidence, while final points are awarded manually by
  the host for humor, usefulness, and character.

## Folder Layout

- [PROJECT_BRIEF.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/PROJECT_BRIEF.md)
- [STATUS.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/STATUS.md)
- [SCENARIO_001_GRANNYS_YARD.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/SCENARIO_001_GRANNYS_YARD.md)
- [CAST_AND_SCORING.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/CAST_AND_SCORING.md)
- [PLAYTEST_PROTOCOL_V0.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/PLAYTEST_PROTOCOL_V0.md)
- [MILESTONES.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/MILESTONES.md)
- [DEVELOPMENT_GUIDE.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/DEVELOPMENT_GUIDE.md)
- [modules](/D:/Repos/Games/TheGame/projects/grannys-house-trials/modules)
- `modules/*` keeps code grouped by domain, but the build currently compiles it as one shared project library
- `modules/sim` for world truth, scenario state, and shared terrain representations
- `modules/playtest` for tester-facing turn packets, transcripts, and evidence-board projections
- `modules/gfx` for render-adjacent but platform-agnostic helpers
- [subprojects](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects)
- [subprojects/grass-field-001/README.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-001/README.md)
- [tests](/D:/Repos/Games/TheGame/projects/grannys-house-trials/tests)
- [assets](/D:/Repos/Games/TheGame/projects/grannys-house-trials/assets)

## Suggested Reading Order

1. [PROJECT_BRIEF.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/PROJECT_BRIEF.md)
2. [STATUS.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/STATUS.md)
3. [SCENARIO_001_GRANNYS_YARD.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/SCENARIO_001_GRANNYS_YARD.md)
4. [CAST_AND_SCORING.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/CAST_AND_SCORING.md)
5. [PLAYTEST_PROTOCOL_V0.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/PLAYTEST_PROTOCOL_V0.md)
6. [DEVELOPMENT_GUIDE.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/DEVELOPMENT_GUIDE.md)

## Status Tracking

Use [STATUS.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/STATUS.md) as the quick truth source for:

- what is complete
- what is only partial
- what is still planned

That file should stay more current than the milestone and brief docs when the
implementation moves quickly.

## Current Direction

The first arc is Granny's House, treated as a small campaign zone rather than
a one-off joke map.

Current milestone focus:

- Milestone 3 is the active path again
- the next major success is a mechanically solid drainage round, not more
  renderer novelty
- the host shell should remain a presentation layer around the shared session

The first round should likely center on a yard-scale water problem, such as
getting water to garden beds without flooding the cellar edge or softening the
path around the house.

The first graphics-side proving slice is intentionally simpler, but it now also
hosts the first real mechanic harness:

- a `100 x 100` field of `1-foot` grass voxels
- authored `1-inch` detail seed data plus a separate `sim::GravityErosionField`
  that owns the current inch-scale settling state
- a separate `sim::AdaptiveTerrainOwnershipField` that classifies which
  `1-foot` blocks are still fully coarse-owned and which upper blocks have
  been promoted to inch-scale refinement
- rendered in a D3D12 window
- lit by a directional sun
- showing gentle terrain variation, a flattened homestead pad, and a small
  garden-bed patch
- inspectable by mouse so the viewer can surface shared voxel facts without
  building the whole game UI first
- a shared Granny's Yard drainage scenario with target-aware legal actions,
  recent evidence, compact tokenized recent events, and objective/failure state
  exposed through the host UI and the agent packet
- the round logic has been pulled behind a dedicated session component so the
  host app no longer owns the test-round orchestration directly

Current renderer status:

- the current `grass-field-001` implementation now renders through a
  shader-side column raycast using uploaded `sim::GrassField` data
- it no longer depends on emitted cube meshes for the visible field
- the grass-field renderer now compiles its HLSL during the build and loads
  precompiled shader blobs at startup instead of runtime `D3DCompile`
- the viewport now uses separate coarse, refined, and hybrid PSOs rather than
  one giant runtime-compiled pixel shader path
- inch-scale gravity erosion is now simulated in a separate sim class and can
  be stepped interactively from the host UI
- the host UI can now switch the viewport between the authored `1-foot`
  coarse columns, a sparse `1-inch` refined-remainder view that only uploads
  promoted patches, and a hybrid adaptive comparison mode
- both refined and hybrid views now traverse the coarse world first and only
  descend into sparse promoted inch patches where refinement actually exists
- the app now also tracks the intended hybrid ownership model: keep full
  `1-foot` blocks coarse, retire only the no-longer-full top blocks, and let
  inch-scale refinement own those mixed volumes
- the same runnable now also hosts the first yard-scale drainage loop:
  route water to the north bed without soaking the cellar edge or softening
  the path, with target-aware actions and factual evidence capture
- the yard round is intentionally implemented as a session boundary so it can
  be reused by future driver apps, tests, and agent-facing tooling
- the square yard size is now configurable in the host UI, and changing it
  resets the field and scenario together
- it is still not a complete arbitrary voxel ray marcher, cube marcher, or
  fully general shader-side voxel traversal renderer
- a fuller arbitrary voxel traversal path is still intended, but has not been
  built yet

The first cast structure is:

- the Builder
- the Chaos Tester
- the Systems Auditor

The first tester-facing protocol should live in a shared `playtest` module.
Driver applications can come later once that surface feels stable.

## Locked Assumptions For Now

- `+Y` is world up for this project line
- the first implementation target is a small D3D12 voxel prototype
- the main format is in-world testing, not raw dev-session footage
- the system records accomplishments and incidents automatically
- final point awards are host-decided, arbitrary, and part of the comedy
- shared code should stay grouped by domain folders, but compile as one small
  project unit until the codebase actually earns more build separation
- tests should stay easy to read and cheap to run
