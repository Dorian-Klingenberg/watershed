# Puzzle Matrix — Comprehensive Report

## 1. What the puzzle matrix is

The puzzle matrix is the design vocabulary for Granny's House Trials water-system
puzzles. It is a structured table of meaningful player–world interactions: what the
player can do, what they do it to, how water responds, what can go wrong, and what
lesson the interaction teaches.

The matrix does three jobs:

1. **Vocabulary** — it names the building blocks. Before a designer can say "this
   puzzle needs a hidden drain betrayal," the hidden drain betrayal has to be
   defined somewhere. The matrix is that definition.

2. **Generator** — any row in the matrix is a candidate puzzle beat. Combine two or
   three rows that create interesting conflicts between their outcomes and you have
   a puzzle sketch.

3. **Scope guard** — the matrix also says what is *not* in the game. If an
   interaction cannot be expressed as a matrix row, it probably does not belong in
   this slice. That keeps scope honest.

## 2. The core formula

```
player action × material/object × water behavior × consequence × lesson
```

Every interaction row in the matrix fills in these five columns. The columns are
not independent — they are a causal chain. Player action causes immediate effect,
which changes water behavior, which produces a consequence, which teaches a lesson.
A row is only useful if you can follow that chain from left to right without
inventing steps.

## 3. What the matrix can and cannot prove

**The matrix can:**

- confirm that a proposed interaction is design-valid (the causal chain is coherent)
- generate candidate puzzle recipes (row combinations that create conflict)
- confirm that motifs are compatible (they share enough prerequisites to coexist)

**The matrix cannot:**

- prove hydraulic solvability — that requires knowing exact elevation, capacity,
  distance, and hazard placement
- prove that a specific map layout has a valid solution — that requires a happy-path
  simulation run
- predict the severity of consequences — a "bank erosion" row might be catastrophic
  on a steep slope and negligible on a flat one; the matrix does not know which

The practical implication: use the matrix to generate intent, use simulation to
validate actual behavior. A matrix combination that passes design review can still
produce a broken puzzle if the geometry makes the happy path impossible.

## 4. Current state of the matrix vocabulary

As of 2026-05-23 the matrix vocabulary includes:

### Interaction table

Ten canonical interaction rows, covering:

- weed clearing in canals and banks
- stone placement and removal
- digging new channels in hard and soft ground
- opening the cistern gate
- repairing an ancient mechanism
- building a weir or check dam
- exposing a buried drain

These cover the most common player tools on the most common materials. They do not
yet cover every combination the axis model can generate.

### Axis model

Six axes defining the combinatorial space:

| Axis | Role |
|---|---|
| Source | where water originates |
| Carrier | how water travels |
| Modifier | what changes the carrier's behavior |
| Target | where water arrives (intended or not) |
| Failure mode | how things go wrong |
| Player tool | what the player can do |

### Motifs

Seven named design patterns:

1. Blockage-as-control
2. Loose-wall temptation
3. Hidden drain betrayal
4. Backwater problem
5. Terrace sequencing
6. Over-success surge
7. Useful danger

Each motif is a reusable systems problem with a lesson, a happy path, and one or
more trap variants.

### Goal model

Eight goal layers above the matrix:

| Type | Purpose |
|---|---|
| Story | human motivation |
| Primary | clear win condition |
| Safety | constraint and tension |
| Stability | system integrity |
| Quality | mastery layer |
| Discovery | world mystery |
| Optional | flavor and replayability |
| Comedy/failure | memorable consequences |

## 5. Design analysis

### What is strong

**The causal chain discipline is correct.** Requiring every row to read left-to-right
as a coherent cause-and-effect sequence prevents vague design. A row that cannot
complete the chain ("player does X, then... something happens with water, lesson:
be careful") is not a row — it is a reminder to think harder.

**The motif system scales well.** Starting with one motif for tutorials and
composing two or three for full puzzles is the right graduation. It means tutorial
puzzles stay readable and competition-round puzzles stay interesting without being
deliberately obtuse.

**Goal conflicts are explicit.** The goal conflict table documents known tensions
(fast flow vs. erosion, clearing blockages vs. stability, shortest route vs. house
safety). Documenting conflicts upfront prevents the designer from accidentally
creating a puzzle with no valid solution because two required goals are mutually
exclusive.

**The matrix is separate from the sim.** The matrix is pure design vocabulary. It
does not contain a hydraulics engine or a flow rate formula. This is correct. The
matrix tells you what interactions matter; the sim decides how large the effect is.
Conflating the two would make both harder to change.

### What is missing or underspecified

**Player tool axis is thin.** The matrix currently names: clear, dig, place stone,
remove stone, repair, open/close, test small flow, wait. These cover the first
round, but the tool vocabulary is the axis most likely to expand as the game adds
more player agency. It needs more explicit entries.

**Time and sequence are not first-class.** The terrace sequencing motif addresses
ordering implicitly, but the matrix has no "this action must happen before that
action" column. A puzzle that requires the player to repair the upper terrace outlet
*before* opening the cistern gate has a sequence constraint, and there is currently
no standard way to express it in a matrix row.

**The failure mode axis is one-directional.** Failure modes describe what goes
wrong. But some failure modes are recoverable (erosion can be slowed, a gate can
be closed) and some are not (a collapsed wall, a flooded basement). That distinction
matters for difficulty tuning and reset policy, and the matrix does not record it.

**No "discovery" row type.** The goal model has a discovery goal layer ("learn what
old mechanism does"). But the matrix rows do not have a discovery analog — a row
type where the "consequence" is information rather than water-system change. Adding
a dedicated discovery row type would make the hidden drain betrayal motif and the
ancient mechanism repair easier to design around.

**Two versions exist.** The codex docs and the worldbuilding bundle both contain a
matrix table, and they are not identical. The worldbuilding bundle has more motifs
(seven vs. five in the codex docs). The codex docs have a more complete YAML
`beat_id` template. These need reconciliation into one canonical form.

## 6. Recommended next steps

### Immediate (design work, no code)

1. Reconcile the two existing matrix tables into one canonical form. Use the
   codex docs as the primary source for row format; use the worldbuilding bundle
   as the primary source for motif completeness.

2. Expand the player tool axis. Name at least twelve tools explicitly, including
   ones not yet in the map (e.g. redirect, divert, plug, measure, wait-and-observe).

3. Add an `action_sequence` field to the puzzle beat template to capture ordering
   constraints.

4. Mark each failure mode as `recoverable` or `permanent` in the motif spec.

5. Add discovery row types to the matrix. These rows have water-system-change = none
   and consequence = information-revealed.

### Near-term (design → wiring)

6. Map every named motif to specific locations on the grass-field-004 authored map.
   Each location becomes a candidate scenario anchor.

7. Write a validation checklist: for a composed puzzle to be accepted, it must pass
   (a) matrix design review, (b) motif compatibility review, (c) goal conflict
   review, and (d) at least one happy-path simulation run.

8. Create a wiring spec document that maps matrix rows to `sim` action types and
   `playtest::GrannysYardSession` legal actions.

### Later (code)

9. A future subproject wires the action anchors, runs a happy-path simulation for
   the first full puzzle composition, and records the evidence.

## 7. Summary

The puzzle matrix is the right design tool for this game. The vocabulary is coherent,
the motif system is practical, and the goal model is well-structured. The main gaps
are: a thin player-tool axis, no sequence constraint column, no recoverability flag
on failures, and two divergent versions of the table that need to become one.

Those are tractable gaps. None of them require changing the fundamental formula.
They are expansions of an already-sound structure.

The matrix is design-ready for the first full puzzle composition. What it is not
yet ready for is direct code wiring — that requires the map-to-anchor mapping and
the happy-path validation step first.
