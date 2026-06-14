# Granny's House Trials Full Brain Dump

## What This Project Actually Is

`Granny's House Trials` is not the full game.

It is a deliberately constrained proof-of-concept inside the larger
`TheGame` repository.

Its job was to answer a very specific question:

> can we take the big world-first, systems-first identity of the larger game
> and compress it into a tiny, funny, replayable, inspectable scenario where
> hidden infrastructure causes real consequences, and where a round-based
> tester-competition format helps reveal system truth rather than replacing it?

The shortest accurate description is:

> a small deterministic consequence sandbox where a hidden environmental
> dependency can be discovered, manipulated, misread, and judged, with the UI
> and cast acting as instrumentation and presentation layers over shared system
> truth

## What It Was Trying To Prove

The project existed because the larger repo's design space is huge:

- ancient passive hydraulic infrastructure
- decaying inherited systems
- uncertain intervention
- cascading consequences
- slow-earned understanding

That space is very easy to overbuild.

`Granny's House Trials` was the answer to:

> what is the smallest slice that still feels true to the parent game's
> identity?

It was trying to prove:

1. a tiny place can still carry meaningful system truth
2. hidden infrastructure can become legible through interaction rather than
   exposition
3. one repair problem can produce multiple outcomes with different downstream
   meanings
4. a round-based playtest format can make system discovery more watchable
5. a host can judge outcomes from evidence instead of the system pretending to
   own all meaning
6. a thin rendering shell can host a reusable simulation and playtest boundary

## The Real Identity

The heart of the project is not "three idiots in a garden."

That is the wrapper.

The real identity is:

- a cozy domestic place
- sitting on top of older buried hydraulic logic
- where intervention seems local but is not actually local
- and where visible consequences reveal structure over time

The comedy is useful only if it increases clarity.

The project consistently tried to avoid becoming:

- random AI-chaos slapstick
- shallow comedy detached from the world
- a generic puzzle box with no systemic truth behind it

The rule was always:

- the world matters more than the cast
- the cast only works if it exposes different truths about the same world

## The Live Scenario

The first real scenario is Granny's Yard.

The tester is in a small yard around Granny's house.

Important space elements:

- garden beds
- a footpath / flat stone run
- vulnerable cellar-adjacent ground
- a drain mouth / drainage route
- a terrace channel area

The objective is:

> get enough water to the garden without damaging the path or soaking the
> cellar edge

The hidden dependency is:

- old drain/foundation logic cross-feeds into an older buried terrace conduit

The intended round shape is:

- the tester sees a small readable place
- the tester acts on incomplete understanding
- the yard responds deterministically
- the hidden structure starts to reveal itself
- the evidence board captures the facts
- the host later judges what the round meant

## The Important Mechanical Truths

The scenario logic is intentionally simple, deterministic, and legible.

Important behavior:

- If no water is routed, nothing meaningful happens.
- If water is routed but the terrace channel is not dug, the path tends to
  soften unless the flat stone run is packed.
- If the terrace channel is dug, water can reach the garden.
- If the buried cross-link remains active and the cellar edge is not hardened,
  routing water can saturate the cellar side.
- Packing the flat stone run disables or mitigates the cross-link.
- Packing the cellar edge hardens that vulnerable zone directly.
- Success is not just "garden got water."
- Clean success means the garden is watered and collateral damage does not
  occur.
- A locally useful route that still damages the yard is still failure.

That last point matters a lot because it preserves the parent game's identity:

- local success can still be global or contextual failure

## How The Project Evolved

The rough evolution was:

### 1. Vertical-slice definition

The project began as a scoped slice inside `TheGame`, with strong emphasis on:

- keeping it small
- preserving the world-first identity
- making the competition a wrapper, not the substance

### 2. Terrain-and-camera proving slice

The first runnable was `grass-field-001`, initially a small D3D12 field viewer
with camera interaction and readable terrain.

This was the Milestone 2 proving layer:

- get a yard-like space on screen
- make it inspectable
- prove a small graphics shell can host the future mechanic

### 3. Renderer scope narrowed intentionally

Rather than trying to invent a general renderer too early, the project settled
on shader-side column raycasting for the current field.

This was one of the best early scope decisions:

- enough rendering to prove the system
- not so much rendering that the slice turns into engine work

### 4. Scenario/mechanic layer emerged

The first real mechanic became Granny's Yard drainage:

- water-routing
- one hidden dependency
- visible consequences
- resettable round state

### 5. Architecture split into truth vs projection

The project then hardened one of its most important boundaries:

- `sim` owns world truth
- `playtest` owns tester-facing projection
- the host shell owns only rendering/UI plumbing

This was a major success.

### 6. Evidence and round summaries became first-class

Milestone 4 added evidence-board logic, projection, and terminal round-state
freezing.

This is the point where the project stopped being just "a mechanic in a visual
harness" and became a real round-based test format.

### 7. Cast layer began conceptually

Milestone 5 started as role framing, personality files, and architecture notes,
but it did not yet become a fully lived runnable layer.

### 8. Drift toward broader systems engineering

By the latest repo state, the project was already becoming more than a game
prototype. It was turning into a reusable pattern for small intervention /
evidence / operator-lens experiments.

## Architecture That Emerged

The current architecture is best understood as:

- `util`
- `sim`
- `playtest`
- `gfx`
- host shell

### `util`

Tiny support types.

Example:

- `NonEmptyString`

### `sim`

The authoritative world/system layer.

Owns:

- terrain fields
- gravity erosion structures
- coarse/refined ownership data
- round logs
- Granny's Yard scenario state
- target visibility
- legal action truth
- action consequences

### `playtest`

The tester-facing layer.

Owns:

- turn packets
- agent command parsing
- evidence-board projection
- tester-role types
- round results
- round summaries
- session orchestration

### `gfx`

Shared rendering-adjacent code.

Owns:

- orbit camera helpers
- the shared D3D12 renderer module

### Host runnable

The host shell should stay thin.

Its job is:

- create the window
- render the world
- host controls
- show the playtest/session output
- route input into the session

The project repeatedly tried to pull real behavior out of `main.cpp`.

## The Strongest Seam: `GrannysYardSession`

The clearest architectural success is `playtest::GrannysYardSession`.

It owns:

- active tester role
- scenario orchestration
- action routing
- recent events
- no-op suppression
- evidence projection
- turn-packet projection
- round result capture
- end-of-round summaries
- reset behavior
- a combined round-presentation surface for the UI

This object is the reason the project can be carried into another workspace as
more than just a UI executable.

## The Playtest Surface

The project created a curated tester-facing surface instead of exposing raw
world state directly.

Important projection types:

- `TurnPacket`
- `EvidenceBoardView`
- `RoundSummary`
- `RoundPresentation`
- `AgentCommand`

That means:

- the world stays truthful in `sim`
- the tester/agent gets a curated operational view
- the host UI has something compact and legible to display
- external agent tooling could consume a structured packet instead of scraping
  random UI

## Evidence Board And End-Round Logic

Milestone 4 turned the slice into a real round-based test harness.

Important current behavior:

- the evidence board is visible in the main runnable
- round evidence is projected into a dedicated board view
- the board can freeze into terminal states
- round states include active, success, failure, and aborted
- resetting or ending a round captures a summary instead of instantly erasing
  meaning
- the summary persists until the next round truly begins

This is essential because the host is supposed to judge from facts, not
reconstruct from memory.

## Why The UI Stayed Dumb

The current UI shell is intentionally thin and somewhat rough.

That was strategic.

The project correctly treated the main unknown as:

- is the system worth anything?

not:

- can we build polished components yet?

So the UI stayed a proving harness while the reusable boundaries were extracted
and hardened.

This is why "good enough for now" was the right call on the current shell.

## Renderer And Performance Decisions

The renderer avoided premature generality.

Key decisions:

- use D3D12
- use shader-side column raycasting
- avoid overcommitting to a full arbitrary voxel traversal path
- compile shaders at build time into `.cso` blobs
- keep separate coarse/refined/hybrid PSOs
- rely on sparse refined patches instead of a fully refined world

The terrain detail model matters:

- the world has coarse `1-foot` voxels
- only selected areas promote to `1-inch` detail
- a hybrid ownership model keeps fully solid columns coarse
- only mixed/detail-relevant volumes descend into refined ownership

That preserved detail where it mattered without exploding traversal and memory
cost everywhere.

## Gravity Erosion And Hybrid Detail

Another important idea is that the project kept surface and ownership concerns
separate:

- `GrassField` holds the coarse authored yard
- `GravityErosionField` owns inch-scale settling / erosion behavior
- `AdaptiveTerrainOwnershipField` tracks whether a column stays coarse-owned or
  becomes refined
- `SparseRefinedPatchField` stores only promoted fine patches

This is not just an implementation detail.

It reflects the broader project habit of:

- building only the precision needed to answer the current question

## D3D12 Renderer Extraction

The repo contains a shared D3D12 renderer module under:

- `modules/gfx/d3d12_renderer`

Its purpose is to centralize:

- device and swap-chain setup
- per-frame management
- GPU buffer allocation
- pipeline creation
- RAII ownership

Important types:

- `D3D12Context`
- `GraphicsFrame`
- `DeviceResources`
- `GPUBuffer<T>`
- `PipelineBuilder`
- `UIFrameRenderer`

This is one area where the code is ahead of some docs.

The module is real, integrated, and tested in live code, even though some docs
still talk as if it is architecture-only future work.

## Testing Philosophy

The testing strategy was fairly disciplined.

The philosophy:

- test pure logic first
- keep the UI shell thin enough that most real behavior is testable
- encode canonical scenario scripts
- test protocol surfaces as well as raw world state
- test reusable renderer infrastructure as library code

Important coverage areas:

- scenario script regression
- round log behavior
- evidence-board projection
- session behavior
- round results and summaries
- turn packet generation
- agent command parsing
- gravity erosion and terrain ownership
- D3D12 renderer abstractions

The scenario regression tests are especially important because they encode the
intended good and bad sequences of the yard.

## Cast Layer And Agent Architecture

Milestone 5 is only partial, but the intended structure is clear.

The three cast roles are:

- Builder
- Chaos Tester
- Systems Auditor

These are meant to be more than cosmetic labels.

They are different interpretive stances toward the same substrate:

- Builder tries plausible intended fixes
- Chaos Tester stress-tests and overreaches
- Systems Auditor inspects, predicts, and explains

The repo also contains an explicit agent-architecture experiment under
`agents/`.

Important decisions there:

- each agent gets identity, soul, instructions, and memory files
- `MEMORY.md` is the rich source of truth
- `MEMORY.slm.md` is the compressed model-facing form
- the approach is model-agnostic
- agents are reactive, not long-lived autonomous systems
- old persistent-lifecycle assumptions were dropped

This matters because `Granny's House Trials` was already moving toward:

- system
- operator lens
- memory artifact
- adjudication

which makes it very portable into a systems-engineering workspace.

## Milestone State

Functionally:

- Milestone 1: complete
- Milestone 2: complete
- Milestone 3: complete
- Milestone 4: complete
- Milestone 5: partial
- Milestone 6: not meaningfully built
- Milestone 7: not meaningfully built

### Milestone 5 specifically

What exists:

- cast role framing
- competition rules and flavor
- personality/agent files
- some tester-role types

What does not yet fully exist:

- cast behavior as a lived runnable layer
- full round-to-round voice/presentation integration

## Workflow And Tracking Decisions

The repo also made some important process decisions:

- prefer WSL/bash for CLI work on this machine when possible
- use the GitHub Project for intermediate milestone/task progress
- treat the repository as the high-level source of truth
- keep repo and project consistent
- do not mark tasks truly complete unless repo reality and project tracking
  agree
- keep milestones high-level and durable
- let lower-level tasks hold implementation detail, history, and backlog notes

These matter because they show the project was already becoming a real
engineering workspace, not just a one-off prototype.

## Rough Git Chronology

Important visible commit landmarks:

- `8ae773d` large early burst of setup and iteration
- `87375d5` renderer workflow improvements and build-time shader work
- `3a33043` shader reorganization
- `57b5c75` recentering with bootstrap/context work
- `a4d56a8` Milestone 4 work
- `332e870` Milestone 4 completion
- `5e20239` Milestone 5 cast-layer files and architecture decisions
- `5492e03` start work on new scenario
- `b88347a` new workspace context + renderer/module additions

That rough chronology is enough to show the arc:

- scaffold
- renderer proving
- mechanic loop
- evidence layer
- cast framing
- broader workspace/handoff preparation

## Honest Gaps And Drift

The project is not perfectly tidy.

Important realities:

- some canonical docs still point toward Milestone 4 even though Milestone 4 is
  complete
- some renderer docs still describe the D3D12 module as architecture-only
- the host-scoring loop is conceptually defined but not fully realized
- the watchable/passive-viewer layer is not complete
- cast integration is partial
- the UI shell is still intentionally a proving harness
- some older cast/tool docs do not fully match the current live playtest
  protocol

So another agent should not assume all docs are synchronized.

## How To Carry This Into A New Workspace

Do not carry forward only the fiction.

Carry forward the pattern:

1. hidden but deterministic system substrate
2. constrained intervention surface
3. factual telemetry / evidence layer
4. multiple operator/tester lenses
5. human interpretation / adjudication layer

In `Granny's House Trials`, those were:

- substrate: yard drainage and buried hydraulic dependency
- intervention surface: route, dig, pack, inspect, advance, reset, end round
- telemetry: evidence board, round log, turn packet, recent events
- operator lenses: Builder, Chaos Tester, Systems Auditor
- adjudication: host-judged competition framing

That five-layer pattern is probably the real asset worth preserving.

## Primary Grounding Files

The strongest source files and docs for another agent are:

- `README.md`
- `PROJECT_BRIEF.md`
- `STATUS.md`
- `MILESTONES.md`
- `PLAYTEST_PROTOCOL_V0.md`
- `SCENARIO_001_GRANNYS_YARD.md`
- `CAST_AND_SCORING.md`
- `modules/sim/src/grannys_yard_scenario.cpp`
- `modules/playtest/src/grannys_yard_session.cpp`
- `modules/playtest/src/turn_packet.cpp`
- `subprojects/grass-field-001/README.md`
- `tests/playtest/scenario_001_script_regression_tests.cpp`
- `modules/gfx/d3d12_renderer/ARCHITECTURE.md`
- `modules/gfx/d3d12_renderer/PHASE_3_COMPLETION.md`

## Bottom Line

The most durable output of `Granny's House Trials` is not a specific joke, UI,
or renderer novelty.

It is a reusable pattern:

- deterministic truth
- curated tester-facing projection
- constrained interventions
- factual evidence capture
- human meaning layered over system facts

That is the part worth preserving in the next workspace.
