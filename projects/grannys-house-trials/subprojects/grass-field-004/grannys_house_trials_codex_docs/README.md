# Granny's House Trials — Codex Starter Pack

This package contains technical design documentation, data schemas, generation algorithms, validation strategy, and reference assets for the first prototype of **Granny's House Trials**, a systems-first tutorial puzzle for the larger ancient-infrastructure water-simulation game.

The immediate engineering target is not a complete game. It is a **small deterministic prototype** that can generate or load a solvable terrain-and-water puzzle, run a simplified water simulation, validate a known happy path, and expose enough debug information for rapid iteration.

## Core concept

The player visits Granny and helps water her garden. It begins as a cozy domestic task, but the garden sits on top of ancient hydraulic infrastructure. The cistern is effectively an infinite water source once opened. The old stone canal is reliable but can be altered. The loose boulder wall is unstable and can fail. The terraced garden is the first simple restoration objective. The lower new garden is downhill, but Granny's house is in the danger zone, and the basement lies under the whole house. The north and west walls of the house are built into a hill, creating seepage/foundation hazards.

The first playable lesson is:

> Water is not a resource you use. Water is a force you release.

## Engineering goal

Build a prototype where Codex can implement:

1. A grid or voxel-like level representation.
2. Terrain cells with materials, elevations, water, stability, saturation, and metadata.
3. A simple deterministic water stepper.
4. Player-style terrain actions: clear weeds, dig, place stones, repair canal pieces, open/close water source.
5. A puzzle matrix and goal model.
6. A puzzle generator that creates a known **happy path** first.
7. A validator that executes the happy path and rejects impossible puzzles.
8. Debug output: ASCII maps, JSON state dumps, and metrics.

## Recommended first milestone

Implement the **static hand-authored level** before procedural generation.

The static prototype should include:

- Infinite cistern at north/top.
- Safe sturdy canal leading to old terraced garden.
- Terraced garden with overflow sequence.
- Granny's house below/downhill with basement hazard zone.
- Loose boulder wall near a tempting route.
- Lower new garden to the south.
- At least one happy path that waters the new garden without flooding the basement.
- At least one trap path that floods or threatens the basement.

## Folder map

- `assets/` — uploaded/generated reference imagery and original PDF.
- `docs/design/` — game design, puzzle matrix, goals, motifs.
- `docs/technical/` — simulation, generator, architecture, data model.
- `docs/validation/` — happy path proof, tests, scoring.
- `docs/implementation/` — milestone plan and Codex task sequence.
- `schemas/` — JSON schemas / proposed formats.
- `examples/` — example puzzle instance and happy path script.
- `prompts/` — prompts/briefs to give coding agents.

## Notes for coding agents

Favor boring, testable code over cleverness. The first implementation should be deterministic, inspectable, and easy to print in logs. Use simple data structures before graphics. Do not start with full fluid dynamics, erosion physics, or WFC. Start with a legible toy model that can prove one happy path works.
