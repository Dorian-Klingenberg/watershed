# Project Brief

## Purpose

`Granny's House Trials` is a vertical-slice project for proving that a small,
systems-heavy game space can support:

- useful testing
- visible consequence
- recurring tester personalities
- a competition format that is funny because it is grounded in real system
  behavior

The competition is not a replacement for the world-first identity of the game.
It is a presentation and development format layered on top of that identity.

## Inherited Canon

This project does not redefine the larger game's world model.

It inherits:

- ancient passive hydraulic infrastructure from [docs/ancient_technology.md](/D:/Repos/Games/TheGame/docs/ancient_technology.md)
- intervention and consequence loops from [docs/core_loops.md](/D:/Repos/Games/TheGame/docs/core_loops.md)
- the broader present-over-ancient layered world model from [docs/game_vision.md](/D:/Repos/Games/TheGame/docs/game_vision.md)
- simulation assumptions from [docs/simulation_model.md](/D:/Repos/Games/TheGame/docs/simulation_model.md)

The project-level docs here should only define what is local to
`Granny's House Trials`.

## What This Project Is Trying To Prove

The project should prove:

1. a tiny ancient-infrastructure location can produce readable cause and effect
2. a round-based test format can make system discovery more entertaining
3. tester personalities can expose different truths about the same mechanic
4. useful development insight can come from a structured in-world session

Current architecture direction:

- `main.cpp` is only the host shell for the runnable
- `playtest::GrannysYardSession` owns the round/session boundary
- the next prototype work should deepen the drainage round, not widen the
  renderer surface
- the next presentation step is a dedicated evidence-board child window fed
  by the shared playtest evidence view

Milestone note:

- Milestone 3 is now the mechanically working drainage round milestone
- the next step is Milestone 4: evidence board presentation and round summary
- keep `main.cpp` thin and let the session boundary remain the round seam

Machine-readable agent context lives at:

- [AGENT_CONTEXT.json](/D:/Repos/Games/TheGame/projects/grannys-house-trials/AGENT_CONTEXT.json)

## What This Project Is Not

This project is not:

- the full game
- a broad simulation sandbox
- an autonomous agent society
- a full fluid solver
- a large settlement sim
- a raw AI-chaos comedy toy with no design discipline

## MVP Scope

The minimum viable version should include:

- a small D3D12 voxel world
- one compact Granny's House yard scenario
- one testable water-routing or drainage problem
- one hidden dependency
- one repeatable round loop
- three tester roles
- a round log / evidence board
- a host-judged scoring screen
- a reliable reset

Implementation note:

- the round log exists and is already surfaced in the host shell
- the scoring screen is still intentionally deferred to later milestone work

## Design Pillars

### 1. World First

The location and its hidden structure matter more than the cast banter.
Comedy should sharpen the audience's understanding of the place, not replace
it.

### 2. Readable Consequence

Every action should create visible, legible state changes.
If a system fails, the failure should be observable in the space.

### 3. Competition Rewards Insight

The winner is not simply "who finished first."
The format should reward discovery, diagnosis, reproducibility, and controlled
success.

### 4. Host Judgment Stays Human

The system records what happened.
The host decides what it meant and how many points it deserved.

### 5. Tiny But Repeatable

The first build should be replayable enough that different tester approaches
feel meaningfully different, even in the same small yard.

## Minimum Viable Location

The first location should be a cozy, readable homestead corner containing:

- Granny's House edge or porch
- garden beds
- a walkable yard or path
- a cellar moisture risk or soft-ground area
- one buried drain, conduit, or terrace channel

## First Mechanic Recommendation

The strongest first mechanic is still water routing.

Candidate round objective:

> get water to the garden beds without flooding the cellar edge or damaging the
> footpath

This works because it is:

- visually obvious
- systemically interesting
- small in scope
- capable of failure
- consistent with the larger game's identity

## Evidence Model

The game should track accomplishments and incidents such as:

- objective completed
- hidden dependency revealed
- failure reproduced
- wrong assumption exposed
- collateral damage caused
- diagnosis made
- number of resets used
- time to outcome

The game should not automatically convert these into the "official" score.

## Host Judging Model

At the end of the round, the host reviews the evidence board and awards points
manually.

This keeps:

- humor
- bias
- theatrical judgment
- room for character

while still preserving a factual record of what actually happened.

## Technical Framing

The likely technical starting shape is:

- D3D12 rendering
- small voxel terrain / structure space
- one interactable system
- one or more resettable round states
- simple tester stand-ins before anything more ambitious
- shared `sim`, `playtest`, and `gfx` modules so later subprojects can reuse
  the same world truth, tester surfaces, and camera helpers

Renderer status note:

- the current proving slice now uses shader-side column raycast rendering for
  the grass field
- it does not yet use a full arbitrary voxel ray marcher or cube marcher
- the next renderer experiments should extend that path without committing to a
  complete arbitrary 3D traversal architecture too early

Milestone 3 note:

- the round logic has been refactored behind a dedicated session component
- that is the architectural seam to build on for the drainage mechanic,
  evidence recording, and future driver apps

## Immediate Success Condition

The first major success condition for this project is:

> one complete round that is understandable, visually legible, and funny
> because the testing revealed a real system truth
