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
- recommended action hints
- recent events

Example shape:
```
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
  "recommended_actions": [
    { "action_id": "inspect_neighborhood", "target": "drain_mouth" },
    { "action_id": "pack_flat_stone_run", "target": "flat_stone_run" }
  ],
  "recent_events": [
    "Round start: the north bed is still dry."
  ]
}
```

## Agent Command JSON

When the tester or agent is ready to act, they emit a small JSON packet that the
driver feeds back into the shared session. The host expects the JSON to contain
an `action_id` property that matches one of the legal action IDs listed above and,
optionally, a `target` (or `focused_target`) string matching one of the visible
targets. `target` may be `null` for area-level actions. Example:

```json
{
  "action_id": "pack_flat_stone_run",
  "target": "flat_stone_run"
}
```

Any parsing errors, missing `action_id`, or unrecognized target names are
reported back to the host, and no action is executed.

`recommended_actions` is an ordered guidance list of action objects, not an
authority list. The host still accepts only legal actions for the current
focused target and round state.

To unlock selection-dependent legal actions, the host also accepts a
`select_block` command that sets the current voxel selection directly from JSON:

```json
{
  "action_id": "select_block",
  "selection_x": 89,
  "selection_y": 0,
  "selection_z": 38
}
```

`selection_x`, `selection_y`, and `selection_z` must be provided together. The
coordinates are validated against the current display grid and column height.
When valid, the host updates selection and refreshes the legal action list.

For targeted commands (for example `inspect_target`, `dig_shallow_channel`, or
`move_terrace_cut`), the host can mirror manual right-click behavior by
selecting a representative block from the target's visible region in the UI
before action resolution, so highlight and info panel state stay in sync with
agent navigation without hardcoded per-target coordinates.

## Validated Scenario 001 Scripts

These scripts are verified against the current scenario implementation and can
be used as regression references for manual clipboard runs.

### Success Script (mitigation-first)

1. `{"action_id":"select_block","selection_x":89,"selection_y":0,"selection_z":38}`
2. `{"action_id":"inspect_neighborhood","target":"drain_mouth"}`
3. `{"action_id":"move_flat_stone_run","target":"flat_stone_run"}`
4. `{"action_id":"pack_flat_stone_run","target":"flat_stone_run"}`
5. `{"action_id":"move_cellar_edge","target":"cellar_edge"}`
6. `{"action_id":"pack_cellar_edge","target":"cellar_edge"}`
7. `{"action_id":"move_terrace_cut","target":"terrace_cut"}`
8. `{"action_id":"dig_shallow_channel","target":"terrace_cut"}`
9. `{"action_id":"move_drain_mouth","target":"drain_mouth"}`
10. `{"action_id":"route_water_source","target":"drain_mouth"}`
11. `{"action_id":"advance_simulation","target":"drain_mouth"}`

Expected terminal outcome:
- `round_status: success`
- `objective completed: 1`
- no collateral damage evidence

### Failure Script (throughput-first)

1. `{"action_id":"select_block","selection_x":89,"selection_y":0,"selection_z":38}`
2. `{"action_id":"route_water_source","target":"drain_mouth"}`
3. `{"action_id":"advance_simulation","target":"drain_mouth"}`
4. `{"action_id":"inspect_neighborhood","target":"drain_mouth"}`
5. `{"action_id":"advance_simulation","target":"drain_mouth"}`
6. `{"action_id":"move_terrace_cut","target":"terrace_cut"}`
7. `{"action_id":"dig_shallow_channel","target":"terrace_cut"}`
8. `{"action_id":"advance_simulation","target":"drain_mouth"}`

Expected terminal outcome:
- `round_status: failure`
- collateral damage evidence present
- `objective failed: 1`

## Outcome Expectations

Action resolution should return:

- success or failure
- observations
- evidence produced this turn

Contradictions and bug suspicion can be layered on later, but the packet shape
should leave room for them now.
