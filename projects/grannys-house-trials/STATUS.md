# Current Status

This document tracks where `Granny's House Trials` actually stands right now.

Its job is to separate:

- completed work
- partially implemented work
- planned but not yet integrated work

This should be kept current as the project evolves.

## Current Phase

The project is currently:

> past the basic scaffold stage, strong on terrain/renderer proving work, and
> still early on the actual "test cast competition round" loop

In plain terms:

- the terrain and rendering proving slice is real and fairly advanced
- the shared scenario and playtest protocol types exist
- the first full in-world testing round is not assembled yet

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

Status: `partial`

What is done:

- gravity erosion stepping on the `1-inch` refinement layer
- sparse refined patch ownership model
- Granny's Yard scenario types in `sim`

What is not done:

- the actual yard-scale water-routing / drainage gameplay loop in the runnable
- visible success / failure outcomes tied to a round objective

### Milestone 4: Evidence Board

Status: `partial`

What is done:

- round-log and evidence-related sim/playtest types
- tested protocol and evidence-board projections in shared code

What is not done:

- evidence-board UI in the main runnable
- end-of-round summary flow

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
- build-time shader compilation with separate coarse / refined / hybrid PSOs
- shader-side column raycast renderer
- coarse `1-foot` field plus `1-inch` refinement concepts for erosion/detail
- adaptive ownership model for coarse vs refined terrain volume
- click inspection and AI-agent JSON snapshot export
- passing unit tests across sim, playtest, gfx, and util

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

> connect the existing Granny's Yard scenario and playtest protocol to the
> runnable so one real test round can be inspected, acted on, and judged

In practical terms, that likely means:

- choose one concrete yard mechanic: water routing / drainage
- expose a tiny set of legal actions in the app
- surface evidence as the player/testers act
- build the first host-judged round summary
