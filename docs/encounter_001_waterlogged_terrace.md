# Encounter 001: Waterlogged Terrace

## Purpose

This encounter is the first local vertical slice for land-use optimization.

It should prove that the core playable loop works without requiring full
downstream simulation:

1. enter a failing site with a visible symptom
2. inspect and read the land with partial information
3. form a local hypothesis
4. make an intervention
5. advance the simulation a little
6. reassess and adjust
7. exit once the site has been pushed into a tolerable state

The design target is not perfect control. The player should be shaping a patch
into an acceptable operating band for agriculture.

## Site Fiction

A village depends on a small terrace field built above older drainage works.
The terrace has become unreliable. Seeds rot in the center, the margins dry
too quickly after improvised drainage, and the villagers no longer understand
why some fixes help for a few days and then fail.

The field sits inside layered history:

- an ancient buried drainage line still partly functions
- a side seep feeds the field from a cracked retaining edge
- a modern berm protects the lower edge from washout but traps moisture

The player is not reclaiming the whole watershed. They are stabilizing one
working patch.

## Player-Facing Symptom

The site should read immediately as agriculturally wrong:

- dark wet center
- patchy reed growth
- sour mud near the old terrace edge
- a few nearly workable tiles at the margin
- villager comments that point at patterns, not solutions

The player should suspect that the field is not uniformly flooded. Different
parts of the site fail in different ways.

## Hidden Structural Truth

The waterlogged center is caused by a combination of:

- ongoing seepage from the western edge
- trapped moisture behind the southern berm
- a buried drain that still works weakly if reopened

This lets the encounter express a key project value:
the symptom is visible, but the real structure must be inferred.

## Prototype Interventions

Keep the first encounter small. The player only needs a few verbs:

- raise ground
- cut shallow channel
- patch or block leak
- open buried drain

Each intervention changes local operating conditions rather than solving the
entire site outright.

## Simulation Shape

The prototype only needs local fields.

### Raw Tile State

- elevation
- saturation
- soil type
- inflow bias
- drain bias
- ancient structure tags
- modern modification tags

### Derived Tile Qualities

Agricultural interpretation should be computed from the raw state:

- too dry
- cultivable
- waterlogged
- unstable wetting
- unstable drying

These are not mere presentation labels. They are the layer through which goals
understand the world.

### Site Metrics

Goals should evaluate site metrics, not raw scalars:

- cultivable tile count
- largest cultivable contiguous patch
- cultivable persistence across short time steps

This creates a maintainable architecture:

raw world state -> derived land qualities -> site metrics -> goal evaluation

## Success Condition

The first encounter should use a composite agricultural goal:

- create at least 12 cultivable tiles
- ensure the largest cultivable contiguous patch is at least 8 tiles
- maintain that condition for 3 simulation steps

This avoids trivial one-tick solutions and reinforces the idea that the player
is establishing a stable local regime.

## Intended Solution Space

The encounter should allow multiple locally valid approaches.

Examples:

- patch the seep, then reopen the buried drain
- cut a modest drainage line, then raise a low interior strip
- reduce inflow first, then shape a patch that holds usable moisture

The encounter should not collapse into a single exact puzzle path.

## Failure Shape

Failure should be legible, not punitive.

Likely bad outcomes:

- the field remains waterlogged
- the center dries but the patch becomes fragmented
- a dramatic drain creates a narrow workable strip instead of a stable field

This supports the game fantasy of local intervention under uncertainty.

## Readability Priorities

The player should mostly read signs, not raw numbers.

Visible cues:

- wet sheen
- reed spread
- contour hints
- dark mud
- drier crust
- unstable edges

Inspection tools can later expose more detail, but the prototype should
already communicate:

- where water is gathering
- where it is leaving
- where a patch is almost usable
- whether the site is settling or still oscillating

## Executable Experiment

This encounter should have its own dedicated executable experiment.

Purpose of the executable:

- instantiate a single local terrace scenario
- run several intervention plans
- derive agricultural land qualities from raw simulation state
- score the site with area, contiguity, and persistence metrics
- make the architecture tangible before a broader game layer exists

The executable should help answer:

- does the local loop feel legible?
- do the chosen interventions create distinct outcomes?
- does the goal architecture read cleanly in code and output?

## Scope Guardrails

This slice should not require:

- regional simulation
- social economy systems
- combat
- inventory management
- generalized quest structure

If this encounter becomes readable, adjustable, and satisfying, it will be a
strong proof that the broader game identity can emerge from local systemic play.
