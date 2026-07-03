# Watershed

Last updated: 2026-06-17

Watershed is a systems-first game and simulation workbench about inherited
infrastructure, partial understanding, and interventions that can help locally
while changing the wider world in surprising ways.

The project is still exploratory. The current work is less about shipping a
finished game loop and more about proving a few hard ideas:

- a world can be built around hidden infrastructure rather than exposition
- player progress can come from better causal understanding, not only loot or
  level gates
- repairs can be meaningful without having a single correct answer
- AI/agent testing can be part of the design process without replacing human
  judgment

## Core Idea

The world is built on the remnants of an ancient water-control civilization.
Aqueducts, pressure systems, passive flow logic, climate-shaping structures,
and layered modern repairs still shape the present, even though most people no
longer understand how the underlying systems work.

The player fantasy is not domination or heroic rescue. It is becoming better at
reading a living system:

1. observe a visible symptom
2. infer hidden dependencies
3. intervene under uncertainty
4. inspect local and downstream consequences
5. update the model of how the world works

The ancient systems are usually treated as passive, fluidic, geometric, and
pressure-mediated. The interesting problems come from decay, modern
workarounds, incomplete knowledge, and second-order effects.

## Current Prototype: Granny's House Trials

The active vertical slice is
[Granny's House Trials](projects/grannys-house-trials/README.md).

Its mission is to prove that a small yard-scale scenario can reveal hidden
infrastructure, record meaningful evidence, and support a real
drainage/repair round without losing the larger world-first identity.

Current slice:

- a deterministic Granny's Yard drainage round
- one objective: route enough water to the north garden bed
- one hidden dependency: a buried cross-link toward the cellar-side runoff path
- visible consequences for garden moisture, cellar dampness, and path damage
- high-level legal actions such as inspect, route, dig, pack, advance, and reset
- a session boundary in `playtest::GrannysYardSession`
- factual evidence capture and evidence-board projection
- compact agent-facing packets for testing and review
- human adjudication kept separate from raw system facts

This is not just a joke test map, and it is not the full game. It is a focused
systems-testing harness for finding out whether the game's core structure can
work at a small, inspectable scale.

## Why This Repo Is Public

This repository is public because the work is easiest to discuss when people
can read the docs and inspect the structure directly.

Professionally, the relevant signal is not simply "a game project." The useful
signal is the system behind it:

- simulation-backed scenario testing
- role-differentiated agent/tester strategies
- evidence capture before human judgment
- deterministic boundaries around AI-facing behavior
- hidden-state diagnosis and consequence tracking
- C++/CMake/D3D12 implementation work
- careful separation between world truth, playtest projection, rendering, and
  host presentation

The portfolio framing is:

> A simulation-backed agent-testing harness where hidden-system scenarios are
> probed through constrained actions, evidence is captured as factual state,
> and a human host reviews what the system actually produced.

The project should stay strange, playful, and world-first. The professional
value comes from accurately describing the systems work that is already here,
not turning the project into an enterprise demo.

## Repository Map

- [AGENTS.md](AGENTS.md) - operating instructions and project identity for
  coding/design agents
- [docs/game_vision.md](docs/game_vision.md) - high-level game vision
- [docs/core_loops.md](docs/core_loops.md) - exploration, intervention, and
  knowledge loops
- [docs/simulation_model.md](docs/simulation_model.md) - layered tile and
  infrastructure simulation model
- [docs/ancient_technology.md](docs/ancient_technology.md) - passive ancient
  infrastructure assumptions
- [docs/systems/fluidic_logic.md](docs/systems/fluidic_logic.md) - pressure,
  siphon, basin, and flow-logic concepts
- [projects/grannys-house-trials/](projects/grannys-house-trials/) - active
  vertical slice
- [experiments/](experiments/) - focused simulation and rendering probes

For the active slice, start with:

1. [projects/grannys-house-trials/README.md](projects/grannys-house-trials/README.md)
2. [projects/grannys-house-trials/STATUS.md](projects/grannys-house-trials/STATUS.md)
3. [projects/grannys-house-trials/PROJECT_BRIEF.md](projects/grannys-house-trials/PROJECT_BRIEF.md)
4. [projects/grannys-house-trials/SCENARIO_001_GRANNYS_YARD.md](projects/grannys-house-trials/SCENARIO_001_GRANNYS_YARD.md)
5. [projects/grannys-house-trials/PLAYTEST_PROTOCOL_V0.md](projects/grannys-house-trials/PLAYTEST_PROTOCOL_V0.md)

## Current Status

As of this README update:

- project scaffold: complete
- first playable/rendered space: complete
- first testable drainage mechanic: complete
- evidence board: complete for the current shell, with richer UI work deferred
- cast layer: partial
- host-judged competition loop: not started

The current implementation includes shared `sim`, `playtest`, `gfx`, and `util`
code, Catch2 tests, a D3D12 grass-field runnable, an authored-map slice, terrain
inspection, adaptive coarse/refined terrain concepts, and evidence projection
for the drainage round.

## Build Notes

The project is Windows/D3D12-oriented. The local CMake presets currently target
Visual Studio 2026 and C++23.

Configure with a preset:

```powershell
cmake --preset vs2026-debug
```

Build the debug preset:

```powershell
cmake --build --preset build-vs2026-debug
```

Run tests:

```powershell
ctest --preset test-vs2026-debug --output-on-failure
```

The active Granny's House target and test details live in
[projects/grannys-house-trials/DEVELOPMENT_GUIDE.md](projects/grannys-house-trials/DEVELOPMENT_GUIDE.md).

## Agent Use

Agents should start with [AGENTS.md](AGENTS.md).

This repo is intentionally a living design/build space. Agents should preserve
the world-first systems identity, check pending transactions before structured
changes, and avoid widening scope before the small vertical slice proves the
core loop.

The current machine-readable project context is:

- [projects/grannys-house-trials/AGENT_CONTEXT.json](projects/grannys-house-trials/AGENT_CONTEXT.json)

The short version:

> Reveal hidden structure. Record evidence. Intervene carefully. Let the world
> answer back.
