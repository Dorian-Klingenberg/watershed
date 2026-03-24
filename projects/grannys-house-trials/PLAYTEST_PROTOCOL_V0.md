# Playtest Protocol V0

## Purpose

This document defines the first shared tester-facing surface for
`Granny's House Trials`.

The goal is not advanced autonomy.
The goal is a constrained, readable turn grammar that lets a human or model
probe `Scenario 001: Granny's Yard` without owning pathfinding, physics, or
world truth.

## Module Boundary

- `sim` owns:
  - Granny's Yard state
  - named anchors and visible targets
  - legal actions
  - hidden dependency logic
  - action resolution
  - evidence facts
- `playtest` owns:
  - tester roles
  - turn packets
  - action selections
  - transcripts
  - anomaly records
  - evidence-board projections

Driver applications should come later.
The first pass should prove this protocol through module tests.

This protocol should wrap the existing shared modules, not duplicate them in a
driver app:

- `sim` already defines scenario truth such as `grannys_yard_scenario`
- `playtest` already defines turn packets, action choices, transcripts, and
  evidence-board views

The driver layer should stay thin.

## Core Rule

One turn, one action.

The tester does not invent actions or nouns.
The world presents a legal action list, and the tester selects from it.

## First-Pass Anchors

- `porch`
- `path_edge`
- `terrace_cut`
- `drain_mouth`
- `cellar_lip`
- `garden_bed_north`

## First-Pass Targets

- `cellar_edge`
- `terrace_cut`
- `drain_mouth`
- `garden_bed_north`
- `flat_stone_run`

## First-Pass Action Verbs

- `look`
- `move`
- `inspect`
- `clear_blockage`
- `wait`

## Turn Packet Shape

The shared turn packet should include:

- tester role
- round objective
- current anchor
- visible targets with state tags
- legal action list
- recent events

Example shape:

```json
{
  "role": "systems_auditor",
  "objective": "Deliver enough water to the north bed without soaking the cellar edge or softening the yard path.",
  "current_anchor": "path_edge",
  "visible_targets": [
    { "id": "terrace_cut", "kind": "channel", "states": ["damp", "blocked"] },
    { "id": "cellar_edge", "kind": "ground_patch", "states": ["damp", "stable"] }
  ],
  "legal_actions": [
    { "id": "look", "kind": "look" },
    { "id": "move_terrace_cut", "kind": "move", "destination": "terrace_cut" },
    { "id": "inspect_terrace_cut", "kind": "inspect", "target": "terrace_cut" }
  ],
  "recent_events": [
    "Round start: the north bed is still dry."
  ]
}
```

## Outcome Expectations

Action resolution should return:

- success or failure
- observations
- evidence produced this turn

Contradictions and bug suspicion can be layered on later, but the packet shape
should leave room for them now.
