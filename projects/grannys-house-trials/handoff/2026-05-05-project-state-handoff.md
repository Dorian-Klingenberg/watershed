# Granny's House Trials Project State Handoff

## Purpose

`Granny's House Trials` is a focused vertical-slice proof of concept inside the
larger `TheGame` repository.

Its real job is not "make a funny Granny map." Its job is to prove that a
small, systems-heavy, inherited-infrastructure scenario can support:

- real intervention under uncertainty
- visible consequence
- factual evidence capture
- repeatable rounds
- multiple tester perspectives
- human host judgment layered on top of system truth

This project is best understood as a small deterministic consequence sandbox
with a theatrical playtest wrapper.

## The Core Idea

The broad repo is about:

- decaying inherited infrastructure
- partial understanding
- repair under uncertainty
- local fixes causing distant or hidden consequences
- learning through interaction instead of exposition

`Granny's House Trials` compresses that into one tiny domestic scenario:

- a yard around Granny's house
- a water-routing/drainage problem
- a buried or half-forgotten hydraulic dependency
- visible collateral damage if the tester solves the obvious problem badly

The "three idiots in the garden" framing is presentation. The real payload is:

- hidden infrastructure
- constrained interventions
- deterministic outcomes
- evidence left behind for review

## What The Project Was Trying To Prove

The project set out to prove:

1. a tiny location can still carry meaningful system truth
2. a round-based testing format can make system discovery more watchable
3. different tester roles can reveal different truths about the same mechanic
4. the host should judge meaning, while the system records facts
5. a thin UI shell can sit on top of reusable simulation and playtest modules

## The Scenario That Exists

The first real scenario is Granny's Yard.

The core objective is:

> get water to the garden beds without soaking the cellar edge or softening the
> path

The hidden dependency is:

- the old drain/foundation logic cross-feeds into an older terrace conduit

The intended pattern is:

- the tester sees a small readable yard
- the tester acts on incomplete understanding
- the yard responds deterministically
- the consequences reveal hidden structure
- the evidence board captures what happened
- the host later judges the round

## Live Mechanical Truths

The current scenario logic is deterministic and intentionally legible.

Important behavior:

- If water is not routed, nothing meaningful happens.
- If water is routed without the terrace channel, the path tends to soften
  unless the flat stone run is packed.
- If the terrace channel is dug, water can reach the garden.
- If the hidden cross-link remains active and the cellar edge is not hardened,
  routing water can saturate the cellar side.
- Packing the flat stone run disables or mitigates the buried cross-link.
- Packing the cellar edge directly hardens the vulnerable cellar-adjacent
  ground.
- Clean success means the garden is watered and collateral damage does not
  occur.
- A "locally successful" route that still damages the path or cellar is treated
  as failure.

That last point is very important because it preserves the broader repo's
identity: local success is not automatically real success.

## Architecture That Emerged

The project evolved toward a clean layered model:

- `modules/util`
- `modules/sim`
- `modules/playtest`
- `modules/gfx`
- thin host runnable(s)

### `sim`

`sim` owns world truth:

- terrain representations
- gravity erosion state
- coarse/refined ownership data
- round log facts
- Granny's Yard scenario state
- visible targets
- legal actions
- action consequences

### `playtest`

`playtest` owns tester-facing projection:

- `TurnPacket`
- `AgentCommand`
- `EvidenceBoardView`
- `RoundResult`
- `RoundSummary`
- `RoundPresentation`
- `GrannysYardSession`

This means the project does not expose raw world truth directly to the UI or to
future agent tooling. Instead, it projects a curated tester-facing surface.

### `gfx`

`gfx` owns rendering-adjacent shared helpers:

- `OrbitCamera`
- the D3D12 renderer module

### Host shell

The host executable should remain thin. Its job is:

- create the window
- render the world
- host UI controls
- show packet/evidence state
- route user input to the session

The project consistently tried to move round logic out of `main.cpp` and into
shared reusable code.

## The Most Important Boundary: `GrannysYardSession`

The strongest architectural seam in the project is
`playtest::GrannysYardSession`.

It owns:

- active tester role
- scenario orchestration
- legal action routing
- evidence projection
- recent event history
- no-op suppression
- round result tracking
- end-of-round summary capture
- reset behavior
- a combined host-facing round presentation snapshot

This is the boundary that made the project reusable beyond one runnable shell.

## The Evidence Layer

Milestone 4 made the project much more real.

The evidence board and summary flow now give the round a factual artifact after
play, instead of leaving the host or viewer to reconstruct what happened from
memory.

Important current behavior:

- the evidence board is visible in the main runnable
- accomplishments, incidents, and discoveries are projected into a board view
- end-of-round states can freeze the board
- explicit round states include active, success, failure, and aborted
- reset/end-round paths preserve a terminal summary before clearing live state

This matters because the system records what happened, while the host decides
what it meant.

## Why The UI Stayed Thin

The current UI shell is intentionally simple and somewhat crude.

That was a deliberate decision, not a failure of effort.

The project decided that the unknown worth solving first was:

- does the systems loop work?

not:

- can we build a polished UI component layer?

So the D3D12/Win32 host stayed a thin proving harness while the shared system
boundaries were extracted and hardened.

## Renderer And Performance Decisions

The renderer deliberately avoided premature generality.

Key decisions:

- use D3D12
- use shader-side column raycasting for the field
- do not overcommit to a full arbitrary voxel traversal path yet
- compile shaders at build time into `.cso` blobs
- keep separate coarse, refined, and hybrid PSOs
- use sparse refinement instead of promoting the entire world to fine detail

The terrain representation is especially important:

- the coarse field is `1-foot` scale
- finer `1-inch` detail exists only in sparse promoted patches
- hybrid ownership keeps fully solid coarse blocks coarse
- only mixed or top-detail areas promote into refined inch-scale ownership

This gave the project readable fine behavior without paying for a fully refined
world everywhere.

## D3D12 Renderer Extraction

The project contains a shared D3D12 renderer module under
`modules/gfx/d3d12_renderer`.

Its intent is to centralize:

- device and swap-chain setup
- frame management
- GPU buffer allocation
- pipeline construction
- RAII resource handling

Important module types include:

- `D3D12Context`
- `GraphicsFrame`
- `DeviceResources`
- `GPUBuffer<T>`
- `PipelineBuilder`
- `UIFrameRenderer`

The code and tests show that this module is real and integrated, even though
some older docs still describe it as earlier-phase future work.

## Testing Philosophy

The testing strategy is one of the project's stronger engineering decisions.

The rule was:

- test pure logic first
- keep the UI shell thin enough that most important behavior is testable
- encode canonical scenario scripts
- test protocol surfaces as well as scenario truth

Important existing test areas include:

- scenario script regression
- evidence-board projection
- round results and round summaries
- session behavior
- packet generation
- agent command parsing
- gravity erosion and terrain ownership
- D3D12 renderer infrastructure

The scenario script tests are especially important because they lock down the
intended good and bad intervention patterns of Granny's Yard.

## Milestone State

As of this handoff:

- Milestone 1: complete
- Milestone 2: complete
- Milestone 3: complete
- Milestone 4: complete
- Milestone 5: partial
- Milestone 6: not meaningfully started
- Milestone 7: not meaningfully started

### What "partial" means for Milestone 5

The cast layer exists conceptually and structurally:

- Builder
- Chaos Tester
- Systems Auditor

It also exists in agent/personality files and protocol framing.

What is still not fully built:

- cast behavior inside the runnable
- round-to-round tester voice/presentation as a lived layer

## Agent Architecture Direction

The repo also contains an agent-layer experiment under `agents/`.

Important decisions there:

- each agent has identity, soul, instructions, and memory files
- `MEMORY.md` is the rich source of truth
- `MEMORY.slm.md` is the compact model-facing form
- the design is model-agnostic
- agents are reactive, not long-lived autonomous processes
- persistent proactive behavior and old OpenClaw assumptions were dropped

That work is relevant because `Granny's House Trials` was already drifting from
"just a game prototype" toward "small system plus operator personas plus memory
artifacts."

## Current Risks And Honest Limitations

The project is not perfectly tidy, and some docs drifted.

Important realities:

- some docs still point toward Milestone 4 even though Milestone 4 is complete
- some renderer docs still describe the D3D12 module as earlier-phase future
  work
- the UI shell is still a proving harness, not a final componentized front end
- the host-judged scoring loop is conceptually defined but not fully realized
- the first watchable audience-ready pass is still not done
- the cast layer is not fully integrated into the runnable yet

Another important reality:

- the current repository was already shifting toward a "new scenario" branch of
  thought, so the durable lesson is the architecture and systems pattern, not
  only the exact Granny's Yard fiction

## Recommended Reframe For The New Workspace

Do not think of `Granny's House Trials` primarily as:

- a joke garden game

Think of it as:

- a small hidden-system substrate
- with constrained interventions
- a factual telemetry layer
- multiple operator/tester lenses
- and a human adjudication layer

That pattern is bigger than Granny's Yard and is likely the real asset you want
to carry into a broader systems-engineering workspace.

## Suggested Primary Files For Future Agents

If another agent needs the most important local grounding, start with:

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
- `tests/playtest/scenario_001_script_regression_tests.cpp`

## Bottom Line

The most durable thing `Granny's House Trials` produced is not a specific UI,
nor a specific garden joke, nor a renderer novelty.

It produced a reusable pattern:

- deterministic world truth
- curated operator-facing projection
- constrained intervention surface
- factual evidence capture
- human interpretation layered over system facts

That is the piece worth preserving.
