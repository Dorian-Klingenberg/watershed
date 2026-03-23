# Future WFC Encounter Design Notes

## Purpose

This note captures the current design direction for using wave function collapse
(WFC) in this project's world-first, systems-first game.

This file is future-facing.
The current runnable prototype in this directory is a scalar-field flooding
experiment, not a WFC implementation.

The goal is not decorative procgen. The goal is to generate small, legible,
solvable encounters shaped by ancient infrastructure, local adaptation, and
downstream consequence.

## Core Direction

The encounter model should combine:

1. WFC for initial spatial coherence
2. Scalar field simulation for per-move world updates
3. A discrete solvability model to prevent hidden dead states

WFC builds a coherent starting situation.
Scalar fields make the situation evolve after each player move.
A higher-level puzzle state model ensures the encounter remains recoverable.

## Why WFC Alone Is Not Enough

Classic WFC is good at:

- placing compatible local states
- producing coherent structure
- making generated spaces feel authored

Classic WFC is not enough to guarantee:

- puzzle solvability
- consequence legibility
- safe interaction with dynamic simulation
- protection from softlocks

Because of that, WFC should be treated as the coherence engine, not the entire
encounter system.

## Layered Tile Model

Each map cell should contain multiple layers rather than a single tile ID.

Suggested layers:

- terrain
- ancient infrastructure
- modern modification
- water state
- condition
- ecology
- usable resource
- optional arcane influence

The discrete tile layers define what a cell is.
The scalar fields define what the cell is currently experiencing.

## Scalar Fields

The first useful scalar fields are:

- water pressure
- saturation
- structural stress

Possible later fields:

- fertility
- contamination
- arcane resonance

These fields update after each player move.
The player acts locally, and the fields propagate the consequences.

## Cell State vs Field State

Example discrete cell layers:

- terrain: clay
- infrastructure: cracked canal
- ecology: reeds

Example continuous fields on that same cell:

- saturation: 0.82
- pressure: 0.67
- stress: 0.74

This allows one structural tile type to behave differently depending on the
current world conditions.

## Airability As A Player-Facing State

For terrain and water encounters, it may be useful to expose a derived
player-facing state called `airability`.

Airability is not simply "less water."
It expresses whether the current soil condition still has enough breathing room
for roots, travel, inspection access, or stable cultivation.

This is useful because it creates a target band instead of a binary objective.

Example state ranges:

- too dry
- airable
- waterlogged

That makes encounter goals more interesting than "drain everything."

Possible goals:

- restore airable soil near a settlement field
- create an airable route to a buried control point
- increase airability in one zone without destroying a wetland elsewhere

Airability can be derived from:

- terrain type
- current saturation
- later, other factors such as compaction, roots, silt, or contamination

This makes it a strong candidate for:

- quest targets
- puzzle success conditions
- diagnostic overlays
- clue systems for players learning to read the land

## Recommended Representation

Use a hybrid model.

Do not encode every possible full tile combination as a separate authored tile.
That will explode in size and become difficult to maintain.

Instead:

- use composite microstates for the hardest interacting core layers
- use overlay layers for lighter or more optional content

Recommended composite core:

- terrain
- infrastructure
- water regime

Recommended overlays:

- ecology
- resources
- effects
- arcane residue

This keeps WFC tractable while still allowing rich combinations.

## Developer-Friendly Authoring

Developers should author data, tags, and rules rather than hardcoded tile
enums.

Useful authoring concepts:

- layer vocabularies
- semantic tags
- compatibility rules
- encounter archetypes
- weighted preferences
- required affordances

Example rule concepts:

- requires
- forbids
- prefers
- connects_to
- propagates
- transitions_under_field_threshold

This makes experimentation easier because new content can be composed from
properties instead of requiring code changes.

## Role of WFC

WFC should answer questions like:

- what kinds of neighboring cells are allowed?
- where do canal fragments, wet soils, routes, and salvage points go?
- how can ancient and modern systems coexist in a coherent local layout?

WFC should not be solely responsible for:

- ensuring puzzle solvability
- simulating consequences
- deciding final encounter quality

## Per-Move Update Loop

Recommended loop:

1. Player takes one action or move
2. Recompute local field sources and sinks
3. Propagate scalar fields for a small bounded update
4. Apply threshold-based state changes
5. Refresh visible clues and local conditions
6. Check success, failure, or recovery conditions

Example:

- player opens a crude gate
- local pressure drops
- downstream flow rises
- nearby clay gains saturation
- a support crosses a stress threshold
- a route becomes muddy

## The Solvability Problem

The encounter may have continuous simulation underneath, but it still needs a
discrete solvability model above it.

This means each encounter should have three representations:

1. Physical state
2. Puzzle state
3. Safety state

### Physical State

The actual layered tiles and live scalar fields.

### Puzzle State

A reduced abstract state derived from the physical state.
This is the state used for solvability reasoning.

Example abstract variables:

- source_to_settlement: connected / weak / broken
- overflow_basin: empty / rising / flooded
- segment_2_wall: stable / stressed / collapsed
- bypass_gate: closed / open / jammed
- path_to_control_node: clear / muddy / blocked
- repair_material_available: yes / no

### Safety State

A set of invariants that must remain true so the encounter cannot silently
softlock.

## Design Rule for Safety

Do not attempt to guarantee that every physical state is solvable.

Instead guarantee:

Every reachable non-failure state must have at least one path to success or one
path to recovery.

This preserves consequence without allowing unfair hidden dead ends.

## Reachable State Categories

Each reachable abstract state should be classified as one of:

- success
- failure
- recoverable
- unsafe transient

Rules:

- every reachable state belongs to exactly one category
- every non-failure state must have a path to success or recovery
- explicit failure states are acceptable if they are clearly telegraphed

## How To Prevent Unsolvable Encounters

Use a combination of:

1. prevalidated state graphs
2. recovery mechanics
3. invariant-preserving simulation guards

### 1. Prevalidated State Graphs

For each generated encounter, explore the reachable abstract state graph under:

- allowed player actions
- automatic field updates

Reject any encounter seed that permits a hidden dead state.

### 2. Recovery Mechanics

Provide at least one costly fallback path when ordinary play goes badly.

Possible recovery tools:

- emergency spill gate
- spirit bargain
- expendable stabilizer
- controlled reset behavior
- costly demolition that reopens a path

### 3. Invariant-Preserving Simulation Guards

If necessary, prevent a field update from silently removing the final recovery
path unless the encounter is clearly entering a declared failure state.

## Encounter Generation Pipeline

Recommended generation sequence:

1. Choose an encounter specification
2. Place mandatory anchors
3. Run WFC around those anchors
4. Initialize scalar fields
5. Derive the abstract puzzle state
6. Simulate player action branches and field updates
7. Reject seeds with hidden dead states
8. Score survivors for clarity, consequence, and variety

Useful anchor examples:

- source
- sink
- blockage
- settlement path
- inspectable clue points
- repair materials
- risky shortcut option

## First Prototype Scope

Keep the first prototype deliberately small.

Suggested encounter:

A canal inspection site on a small grid where the player must restore safe flow
to a settlement route.

Suggested requirements:

- one water source
- one damaged canal segment
- one improvised village bypass
- one fragile travel route
- one or two repair interactables
- at least two possible interventions
- one tempting local fix with worse downstream consequences
- one clue that points toward the safer option

Suggested first field set:

- water pressure
- saturation
- structural stress

This should be enough to test:

- layered state generation
- field-driven consequence
- discrete solvability tracking
- recovery from bad moves
- encounter validation

## Summary

The emerging design is:

- layered, systems-aware WFC
- bounded scalar field updates after each player move
- abstract solvability tracking above the simulation
- rejection of seeds with hidden dead states
- recoverable consequence rather than unfair softlock

That structure fits the project's core identity:

- ancient infrastructure
- partial understanding
- local intervention
- downstream consequence
- discovery through systemic interaction
