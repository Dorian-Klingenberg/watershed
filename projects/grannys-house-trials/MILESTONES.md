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

Do not move into:

- larger maps
- multiple settlements
- advanced AI autonomy
- broad inventory systems
- deep narrative scripting
- driver applications under `subprojects/`

until Milestone 6 is working.
