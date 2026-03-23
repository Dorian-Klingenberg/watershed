# Iteration 1, Sprint 1

## Goal

Build the smallest useful prototype for experiment 1:

- build a 3-layer scalar-field flooding sandbox
- simulate flooding over time
- observe how terrain and spring placement shape saturation spread

This sprint is not about full encounter design yet.
It is about establishing a clean, testable foundation.

## Scope

Included:

- terrain layer
- water feature layer
- initial saturation state
- one timestep flood simulation
- simple text visualization
- deterministic tests

Deferred:

- devices
- player actions
- solvability graph
- multiple water feature types beyond a spring source
- arcane influence
- data-driven authoring

## Sprint Outcome

By the end of this sprint, we want to be able to:

1. construct a small map with a known terrain layout
2. place spring sources in that layout
3. simulate saturation over multiple timesteps
4. print the landscape in a way that makes flooding behavior readable
5. verify deterministic behavior with a small test suite

## Domain Model

Keep the first model deliberately small.

Suggested types:

- `TerrainType`
- `WaterFeatureType`
- `CellState`
- `GridState`
- `TerrainProperties`
- `SimulationStepResult`

Suggested first terrain set:

- `bedrock`
- `clay`
- `loam`
- `sand`

Suggested first water feature set:

- `none`
- `spring_source`

## Suggested Build Order

### 1. Grid and Cell Foundations

Implement:

- coordinate handling
- grid storage
- cell access
- bounds checking

Tests:

- cells store terrain and water feature correctly
- grid indexing is stable
- out-of-bounds handling is correct

### 2. Terrain Properties

Implement a simple lookup for terrain behavior.

Useful properties:

- absorption
- retention
- lateral spread

Tests:

- each terrain returns expected coefficients
- lookup is deterministic

### 3. Runtime Saturation

Add live saturation as a runtime value on each cell.

Important note:

Treat saturation as simulated state, not as a permanently fixed authored tile
identity.

A future procgen step may define an initial saturation band, but the simulation
should own the live value after setup.

Tests:

- saturation initializes correctly
- saturation clamps to valid bounds

### 4. Manual Test Map

Create a hardcoded map before adding any procgen.

This gives a stable sandbox for flood behavior tuning.

Tests:

- the test map initializes correctly
- spring placement is correct
- text output matches the expected arrangement

### 5. Single Timestep Flood Simulation

Implement one pure update step:

- springs add water
- neighbors exchange saturation
- terrain affects spread behavior
- values are clamped

Prefer a function like:

`next_state = simulate_step(current_state)`

Tests:

- a spring increases local saturation
- a map with no springs does not gain water spontaneously
- sand spreads water faster than clay
- bedrock absorbs less than loam
- identical inputs produce identical outputs

### 6. Multi-Step Simulation

Run repeated timesteps and inspect how wet zones evolve.

Tests:

- repeated steps remain numerically stable
- saturation stays bounded
- wetness propagates outward from a spring over time

### 7. Text Visualization

Add readable console output for:

- terrain
- spring locations
- saturation intensity

The first prototype does not need graphics.
Readable text output is enough for tuning.

Tests:

- render functions do not crash
- known maps render expected symbols

### 8. Regression Seeds

Choose a few known seeds and assert high-level properties.

Examples:

- number of spring cells
- number of wet cells after 10 steps
- max saturation after 20 steps
- whether flooding remains localized or spreads broadly

Prefer high-level assertions over brittle full-map snapshots at this stage.

## Architecture Guidance

To keep this sprint clean and testable:

- prefer pure functions
- pass RNG explicitly
- avoid global mutable state
- separate generation from simulation
- separate simulation from rendering

Recommended responsibility split:

- a future generator will build initial layout
- `FloodSimulator` advances the world one step
- `GridState` stores current state
- rendering helpers only display state

## Test Strategy

Use three layers of tests:

### Unit Tests

For:

- grid behavior
- terrain property lookups
- one-step flood update rules

### Determinism Tests

For:

- seed reproducibility
- stable timestep results from fixed inputs

### Lightweight Regression Tests

For:

- a few known seeds with expected broad behaviors

## Definition of Done

Sprint 1 is done when:

- a 3-layer map can be created and run consistently
- a spring source can flood the landscape over time
- terrain types produce visibly different spread behavior
- the output is readable enough to tune by observation
- core behavior is covered by deterministic tests

## Nice-To-Have If Time Allows

- initial saturation categories at generation time
- a side-by-side print of terrain and live saturation
- a command-line seed override
- a compact debug summary per timestep

## Summary

Sprint 1 should prove the foundation:

- a scalar-field flooding model can create a coherent evolving landscape
- a simple timestep model can evolve that landscape
- the prototype is deterministic, inspectable, and easy to test

That is the right base before adding richer encounter logic.
