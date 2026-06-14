# Implementation Milestone Plan

## Milestone 0 — Repository skeleton

Deliverables:

- source folder
- tests folder
- examples folder
- schemas folder
- CLI entrypoint
- README with run instructions

Success:

- `python -m ght --help` or equivalent runs.
- Test runner works.

## Milestone 1 — Static grid and ASCII renderer

Deliverables:

- `TerrainGrid`
- `Cell`
- `Material`
- ASCII renderer
- load static JSON puzzle

Success:

- Can print a map showing cistern, canal, terraces, house, boulder wall, new garden.

## Milestone 2 — Basic deterministic water

Deliverables:

- source inflow
- water transfer to lower neighbors
- absorption/saturation
- simple metrics

Success:

- Opening cistern causes water to move downhill through canal.
- Garden hydration changes.

## Milestone 3 — Actions

Deliverables:

- `clear_weeds`
- `dig`
- `place_stone`
- `repair`
- `open_gate`
- `wait`
- action script runner

Success:

- Happy path script changes terrain/features and runs simulation.

## Milestone 4 — Goals and validation

Deliverables:

- goal evaluator
- metrics comparison
- pass/fail report
- event log

Success:

- Static happy path passes.
- Straight-downhill trap fails.

## Milestone 5 — Hazards

Deliverables:

- basement seepage rules
- boulder wall stability rules
- basic erosion rules
- hidden drain rule

Success:

- Bad routes create legible failures.
- Failure appears in metrics and event log.

## Milestone 6 — Puzzle matrix and motif data

Deliverables:

- YAML/JSON motif definitions
- goal package definitions
- generator can choose a goal and motifs

Success:

- Generator outputs an abstract puzzle recipe.

## Milestone 7 — Graph-based layout generator

Deliverables:

- place source, terraces, house, new garden, safe path, trap path
- rasterize to grid
- attach happy path script

Success:

- At least 10 seeds generate valid-looking maps.

## Milestone 8 — Generate and validate

Deliverables:

- automatic generation loop
- happy path validator
- trap validator
- scoring and rejection

Success:

- Generate 100 seeds and accept a subset that pass validation.

## Milestone 9 — Visualization hooks

Deliverables:

- export PNG/SVG debug map, or simple web view
- per-cell water/elevation overlays

Success:

- Designer can inspect why puzzle passed/failed.

## Milestone 10 — Prepare engine port

Deliverables:

- stable JSON schema
- rule versioning
- deterministic test corpus
- C++-friendly data structures

Success:

- Simulation rules are documented enough to port.
