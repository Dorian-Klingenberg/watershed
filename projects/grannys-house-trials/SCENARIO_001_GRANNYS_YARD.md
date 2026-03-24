# Scenario 001: Granny's Yard

## Purpose

This scenario is the first proposed round space for `Granny's House Trials`.

It should serve as:

- the first playable yard
- the first introduction to the tester cast
- the first proof that cozy domestic space can hide meaningful ancient
  infrastructure
- the first repeatable competition round

## Tone

The space should feel:

- warm
- practical
- inhabited
- slightly improvised
- quietly threatened by deeper system logic

The point is not immediate grandeur.
The point is contrast:

- a humble household problem
- over a buried ancient water system

This should preserve the repo-wide layered-history idea:

- present-day domestic adaptation on top
- older hydraulic logic underneath
- a local fix that may accidentally disturb inherited structure

## Physical Layout

The first-pass layout should likely include:

- one side of Granny's House
- a small porch or work area
- a path through the yard
- garden beds
- a low wet patch or soft ground zone
- a cellar edge, foundation line, or drainage risk area
- one buried channel or drain tied to older infrastructure

## Named Anchors

For the first playtest-facing pass, the yard should expose a small stable set
of named anchors rather than freeform coordinates.

Recommended anchors:

- `porch`
- `path_edge`
- `terrace_cut`
- `drain_mouth`
- `cellar_lip`
- `garden_bed_north`

These are not the whole world.
They are the first parser-stable places from which testers can observe or act.

## Round Objective

Recommended first objective:

> deliver enough water to the garden beds while avoiding obvious damage to the
> cellar edge and yard path

This gives us:

- a clear visible target
- room for overwatering
- room for underwatering
- room for misrouting
- room for hidden dependency reveal

## Hidden Dependency

The scenario should include one buried or misunderstood hydraulic feature.

Good candidate:

- an old foundation drain that cross-feeds into an ancient terrace conduit

That hidden route should behave like passive hydraulic logic, not a machine:

- thresholds
- trapped water
- pressure bias
- terrain-guided routing

What this enables:

- a "reasonable" yard fix that causes trouble somewhere else in the same space
- a reveal that the house is sitting inside older infrastructure logic

## Visible Targets And Keywords

The first constrained protocol should expose a small noun list tied directly to
the scenario.

Recommended visible targets:

- `cellar_edge`
- `terrace_cut`
- `drain_mouth`
- `garden_bed_north`
- `flat_stone_run`

Recommended first-pass state tags:

- `dry`
- `damp`
- `wet`
- `soft`
- `stable`
- `unstable`
- `flowing`
- `blocked`
- `revealed`

## Good Failure Modes

The first scenario should support at least three readable failure outcomes:

- the garden stays too dry
- the cellar edge gets too wet
- the path becomes soft or unstable

Optional fourth:

- water vanishes into the wrong buried route, proving there is hidden
  structure nobody accounted for

## Evidence Worth Tracking

Scenario telemetry should make it easy to record:

- whether the beds were watered
- whether the cellar edge flooded
- whether the path softened
- whether buried routing was exposed
- how many interventions were attempted
- how long the round took
- whether a tester prediction was vindicated

## Tester Opportunities

### Builder

Should have a plausible intended fix path that can work if carefully handled.

### Chaos Tester

Should have obvious ways to over-apply water, reroute flow recklessly, or test
the wrong thing in a way that still teaches us something.

### Systems Auditor

Should have enough information to make strong predictions and call out weak
assumptions, even if they do not physically execute the cleanest solution.

## Round Flow

1. Introduce the yard and the stated objective
2. Show the current score categories or evidence categories
3. Let testers inspect or act
4. Surface the system response
5. Reveal at least one meaningful consequence
6. Present the evidence board
7. Let the host award points
8. Reset for another attempt

## Atomic Playtest Actions

The first tester-facing surface should stay intentionally small.

Recommended first-pass actions:

- `LOOK`
- `MOVE <anchor>`
- `INSPECT <target>`
- `CLEAR_BLOCKAGE <target>`
- `WAIT`

No action should require the tester to supply a multi-step plan.
The world should present only legal actions from the current anchor.

## Why This Scenario Matters

If this scenario works, it proves:

- the game can begin small without losing its identity
- hidden infrastructure can matter in a domestic space
- tester competition can expose meaningful system truth
- the audience can understand a round quickly
