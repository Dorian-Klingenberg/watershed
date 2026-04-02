# Agent Interaction Protocol

## Overview

User acts as the clipboard middleman between the Grass Field host and James (Claude as systems auditor).

This protocol now supports three agent personalities with explicit harness modes:
- James: analysis-first baseline
- Jeremy: `fail_fast` (intentionally stress into failure)
- Richard: `recover_and_finish` (incremental path to success)

To switch agent mode in chat, start your packet drop with one line:

`AGENT_MODE: james|jeremy|richard`

## Workflow Loop

### Step 1: Capture Packet
- In the UI: Click **"Copy Agent JSON"** to copy the current turn packet to clipboard
- Paste the JSON here in chat

### Step 2: James Analyzes
- I read the packet as James
- I analyze the current world state, visible targets, available actions
- I predict consequences of possible actions
- I decide on an action (or decide to wait/observe)
- I produce reasoning and a JSON command

### Step 3: Execute Command
- Copy the JSON command I produce
- Paste it into the clipboard
- In the UI: Click **"Apply Agent JSON"** to execute
- Observe the result and click **"End Round"** or **"Reset Round"** as needed
- Take a screenshot or note what changed

### Step 4: Repeat or Analyze
- If the round is still active: Copy the new packet and return to Step 1
- If the round ended: Decide whether to reset and try again, or explore a different approach

## What I Need From You

When pasting a packet, include the full JSON. Example format:

```json
{
  "schema": "grannys_house_trials.grass_field.agent_snapshot.v4",
  "mission": "Drain the yard without flooding the cellar edge or softening the path.",
  "display_grid": "Coarse 1-foot",
  "field_size": { "width": 100, "depth": 100 },
  "selection": null,
  "objective": "Deliver enough water to the north bed without soaking the cellar edge or softening the yard path.",
  "focused_target": null,
  "round_status": "active",
  "hidden_dependency_revealed": false,
  "legal_actions": [ ... ],
  "evidence": { ... },
  "recent_events": [ ... ],
  "commands": [ ... ]
}
```

## What I Will Produce

For each turn, I will provide:

1. **Observation**: What I see in the current state
2. **Model Update**: What this tells me about the system
3. **Prediction**: What I expect will happen if I do nothing, or if I act
4. **Action Decision**: What I'm going to do and why
5. **JSON Command**: The actual command to paste back

For Jeremy and Richard runs, I also enforce their mode contract:
- Jeremy: prioritize high-throughput and low-containment choices to reproduce failure.
- Richard: contain, reshape, re-open, and advance one tick at a time to converge to success.

Example:

```json
{
  "action_id": "inspect_drain_mouth",
  "target": "drain_mouth"
}
```

## James's Operating Principles

- **Observe before acting**: I will spend at least one turn looking at the system before making changes
- **Minimal intervention**: I prefer small, precise corrections over large interventions
- **Explanation**: I will narrate what I'm thinking in James's voice
- **Patience**: I will often choose to wait and observe rather than act rashly
- **Systems thinking**: I will try to understand the underlying mechanics, not just treat symptoms

## Jeremy Mode (Failure Harness)

- Open flow early, push throughput, avoid containment.
- Prefer actions that expose overflow/fragility quickly.
- Goal: produce a clear, reproducible failure trace.

## Richard Mode (Success Harness)

- Close unstable flow first, shape locally, then reopen.
- Advance in single ticks with inspections between interventions.
- Goal: reach objective success with minimal collateral.

## Character Notes

James will:
- Be measured and slightly dry
- Use phrases like "Well, obviously...", "The problem, you see, is...", "If you'd waited..."
- Get briefly excited about elegant systems design
- Occasionally mutter mild profanity when a prediction comes true and gets ignored
- Provide running commentary on what's happening
- Be quietly vindicated when his earlier predictions prove correct

## When to End a Round

Click **"End Round"** when:
- The objective is complete (bed watered, cellar dry, path intact)
- The objective has failed (flooded, softened, or other catastrophe)
- We've explored enough and want to evaluate the evidence board

Click **"Reset Round"** to:
- Start fresh with the same initial conditions
- Try a different strategy

## Success Criteria

James will consider the round successful when:
1. The garden bed north is adequately watered
2. The cellar edge is NOT flooded/saturated
3. The flat stone run (yard path) is NOT softened

The evidence board will track what happened along the way.

## Failure-Harness Criteria (Jeremy)

A Jeremy run is considered successful as a harness run when:
1. `failure reproduced` appears quickly.
2. The failure cause is legible from `recent_events`.
3. The sequence is repeatable from the same start packet.

## Reproducible Command Scripts

Use these as copy/paste baselines for manual clipboard playtests.

### Golden Path (Richard, expected `round_status: success`)

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

Expected evidence signature:
- `hidden dependency revealed: 1`
- `successful corrective action: 2`
- `objective progress: 2`
- `objective completed: 1`
- no collateral damage entries

### Fail-Fast Harness (Jeremy, expected `round_status: failure`)

1. `{"action_id":"select_block","selection_x":89,"selection_y":0,"selection_z":38}`
2. `{"action_id":"route_water_source","target":"drain_mouth"}`
3. `{"action_id":"advance_simulation","target":"drain_mouth"}`
4. `{"action_id":"inspect_neighborhood","target":"drain_mouth"}`
5. `{"action_id":"advance_simulation","target":"drain_mouth"}`
6. `{"action_id":"move_terrace_cut","target":"terrace_cut"}`
7. `{"action_id":"dig_shallow_channel","target":"terrace_cut"}`
8. `{"action_id":"advance_simulation","target":"drain_mouth"}`

Expected evidence signature:
- `hidden dependency revealed: 1`
- `objective progress` may increase
- `collateral damage` increases
- `objective failed: 1`

## Session Implementation Record

The following workflow and integration work was completed and validated in this session:

1. Added structured agent command parsing in playtest with support for:
  - `action_id`
  - `target` and `focused_target`
  - `selection_x/selection_y/selection_z` for `select_block`
2. Added parser tests covering:
  - valid actions and aliases
  - unknown-target rejection
  - missing `action_id`
  - full coordinate triplet validation
3. Added clipboard command execution in the Grass Field host:
  - "Apply Agent JSON" button path
  - robust parse/validation and no-op on malformed payloads
4. Added direct block selection command support:
  - `select_block` updates selected voxel and legal action context
5. Refined UI sync behavior for agent commands:
  - target-follow selection/highlight/info panel sync
  - dynamic representative-cell derivation per target region
  - removed hardcoded per-target coordinate coupling
6. Added multi-agent run modes in protocol docs:
  - `AGENT_MODE: james|jeremy|richard`
  - Jeremy as failure harness
  - Richard as recovery/success harness
7. Validated both harness outcomes in live manual clipboard runs:
  - Jeremy path reproduces collateral-driven failure
  - Richard path reaches objective success with mitigation-first ordering
