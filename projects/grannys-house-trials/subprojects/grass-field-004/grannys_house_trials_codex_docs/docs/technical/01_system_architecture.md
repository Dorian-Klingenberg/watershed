# System Architecture

## Target architecture

The prototype should be structured around a deterministic simulation core with optional visualization layered on top.

```text
+---------------------------+
| CLI / Debug UI / Renderer |
+-------------+-------------+
              |
+-------------v-------------+
| Scenario Runner            |
| - load puzzle               |
| - apply action script       |
| - step simulation           |
| - report metrics            |
+-------------+-------------+
              |
+-------------v-------------+
| Simulation Core             |
| - terrain grid              |
| - water solver              |
| - material rules            |
| - seepage/stability rules   |
| - goal evaluator            |
+-------------+-------------+
              |
+-------------v-------------+
| Puzzle Model                |
| - cells                     |
| - objects/features          |
| - goals                     |
| - happy path actions        |
| - motifs                    |
+-------------+-------------+
              |
+-------------v-------------+
| Generator / Validator       |
| - graph grammar             |
| - matrix selection          |
| - layout generation         |
| - happy path validation     |
+---------------------------+
```

## Recommended first language/implementation

Use whatever is fastest for iteration. Python is fine for generation and validation. C++ is fine if the purpose is engine practice. The architecture should remain language-neutral.

For Codex, a practical sequence is:

1. Implement in Python first for clarity and speed.
2. Once rules stabilize, port simulation core to C++.
3. Keep JSON puzzle schema compatible across both.

## Determinism requirements

All generation and simulation must be deterministic given:

- random seed
- puzzle definition
- action script
- simulation rule version

Every run should produce the same metrics and event log.

## Major modules

### TerrainGrid

Stores cells and neighbor lookup.

### MaterialRules

Defines water capacity, absorption, erosion resistance, stability, and diggability.

### WaterStepper

Moves water between cells based on elevation, water height, capacity, and resistance.

### ActionSystem

Applies player-like actions to cells and objects.

### GoalEvaluator

Computes metrics and determines success/failure.

### PuzzleGenerator

Creates puzzle instances from motifs and goal packages.

### HappyPathValidator

Runs known solution script and checks goals.

### TrapValidator

Runs known wrong scripts and checks that failure is interesting and legible.

### DebugRenderer

Prints ASCII maps and dumps JSON state.

## First deliverable

A command like:

```bash
python -m ght run examples/ght_static_001.json --script examples/happy_path_001.json --steps 200 --dump out/
```

Expected output:

```text
Puzzle: GHT_STATIC_001
Script: happy_path_001
Result: SUCCESS
old_garden_hydration: 84.0
new_garden_hydration: 76.5
basement_dampness: 12.0
boulder_wall_stability: 91.0
erosion_damage: 8.0
```
