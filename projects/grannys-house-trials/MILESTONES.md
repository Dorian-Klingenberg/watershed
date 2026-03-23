# Milestones

## Purpose

This document breaks `Granny's House Trials` into implementable slices.

The main rule is:

> every milestone should move us toward one complete, legible, replayable
> in-world testing round

## Milestone 1: Project Scaffold

Deliverables:

- project folder
- core docs
- scope boundaries
- first scenario choice
- first cast / scoring model

Success condition:

- the project has a stable identity in the repo

## Milestone 2: First Playable Space

Deliverables:

- D3D12 app shell
- camera and movement
- tiny Granny's Yard space
- basic voxel terrain / structure representation

Success condition:

- we can move around a readable homestead yard

## Milestone 3: First Testable Mechanic

Deliverables:

- simple water-routing or drainage interaction
- visible success / failure states
- one hidden dependency
- scenario reset

Success condition:

- the yard can produce at least two meaningfully different outcomes from
  different interventions

## Milestone 4: Evidence Board

Deliverables:

- tracked round accomplishments
- tracked incidents
- tracked discoveries
- round summary UI

Success condition:

- a round leaves behind enough evidence to judge what happened

## Milestone 5: Cast Layer

Deliverables:

- Builder role framing
- Chaos Tester role framing
- Systems Auditor role framing
- placeholder reactions or dialogue hooks

Success condition:

- the round already feels different depending on which tester is acting or
  speaking

## Milestone 6: Host-Judged Competition Loop

Deliverables:

- end-of-round scoring screen
- manual point assignment workflow
- winner declaration
- reset into next round

Success condition:

- one full round can be played, reviewed, judged, and restarted

## Milestone 7: First Watchable Pass

Deliverables:

- clearer presentation
- better readability of outcomes
- first round that is understandable to a viewer

Success condition:

- someone unfamiliar with the project can watch one round and understand the
  goal, the failure, and the ruling

## Current Recommendation

Do not move into:

- larger maps
- multiple settlements
- advanced AI autonomy
- broad inventory systems
- deep narrative scripting

until Milestone 6 is working.

