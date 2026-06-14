# Codex Task Sequence

Use these as small, direct coding-agent tasks.

## Task 1: Create project skeleton

Create a Python package named `ght` with modules:

- `grid.py`
- `materials.py`
- `features.py`
- `actions.py`
- `simulation.py`
- `goals.py`
- `scenario.py`
- `render_ascii.py`
- `cli.py`

Add `tests/` and `examples/` folders.

## Task 2: Implement data classes

Implement simple data classes for:

- `Cell`
- `MaterialRule`
- `Feature`
- `Goal`
- `Action`
- `ActionScript`
- `Puzzle`
- `Metrics`

Prefer plain dataclasses and type hints.

## Task 3: Load example puzzle JSON

Implement a loader that reads `examples/ght_static_001.json` into the model.

Validate:

- grid size matches cells
- all materials referenced exist
- feature cells are in bounds
- action script targets exist

## Task 4: ASCII renderer

Render map using symbols:

```text
C = cistern/source
= = sturdy canal
T = terrace
H = house
B = basement hazard zone or house footprint debug
R = loose boulder wall
N = new garden
w = weeds
~ = water
. = soil/lawn
^ = hill/high ground
# = stone
```

## Task 5: Implement water stepper

Implement simple deterministic flow from high surface to low surface.

Keep it readable and heavily tested.

## Task 6: Implement action runner

Apply actions by tick.

Actions:

- `open_gate`
- `clear_weeds`
- `repair`
- `dig`
- `place_stone`
- `wait`

## Task 7: Implement metrics and goals

Compute:

- `old_garden_hydration`
- `new_garden_hydration`
- `basement_dampness`
- `boulder_wall_stability`
- `erosion_damage`

Evaluate goals from JSON comparisons.

## Task 8: Implement static happy path test

Create a test that runs `happy_path_001.json` and asserts success.

## Task 9: Implement trap scripts

Create tests for:

- straight downhill route
- full gate too early
- hidden drain use

Each should fail safety goals and produce clear events.

## Task 10: Implement first generator

Do not use WFC yet.

Create a simple seeded generator that places:

- source north
- canal to terraces
- house south of terraces
- new garden south/east or south/west
- safe bypass path
- boulder wall trap path

Attach generated happy path script.

## Task 11: Implement generation validation loop

Generate N puzzles, run happy path, accept/reject, write reports.

## Task 12: Export LLM-readable puzzle reports

For each accepted puzzle, output Markdown:

- map summary
- goals
- known happy path
- trap paths
- final metrics
- event log highlights
- ASCII maps
