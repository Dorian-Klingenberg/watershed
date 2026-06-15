# Grass Field 005 — Plan

## What this subproject is

`grass-field-005` is a design consolidation and expansion arc. Its job is to
take the puzzle matrix vocabulary that was sketched in two separate doc bundles
in `grass-field-004` and turn it into a coherent, authoritative reference that
can drive scenario wiring in a future code subproject.

This subproject has no code. It will be ready when:

- the consolidated matrix table is canonical and complete
- all motifs are fully specified with happy path, trap path, and failure modes
- the composition guide gives a designer enough structure to create a new puzzle
  from scratch without guessing
- at least one full worked puzzle composition is documented and analyzed
- a lesson file walks through the whole system for a new collaborator

## What this subproject is not

This is not:

- a runnable
- a renderer experiment
- a scenario-wiring task (that belongs to a later subproject)
- a specification of the simulation mechanics themselves

The matrix is a **design vocabulary**, not a simulation ruleset. The vocabulary
tells you what interactions are meaningful and why. The simulation spec tells you
how to compute them. These are separate concerns.

## Design goals for this arc

1. **Consolidate** — the codex docs and worldbuilding bundle have two slightly
   different versions of the matrix. Pick one canonical form and reconcile the
   differences.

2. **Expand** — the existing tables have ~10 interaction rows. The axis model
   supports many more. Add rows that cover important combinations not yet named.

3. **Catalog all motifs** — seven motifs are documented but in different places
   and at different levels of detail. Bring them all to the same level.

4. **Build the composition guide** — the "motif composition rule" exists as one
   paragraph. It needs to become a full guide with worked examples.

5. **Teach the system** — write a lesson file that a new collaborator (or a
   returning agent) can read to understand the full matrix system in one sitting.

## Future wiring (out of scope here)

When the design arc is done, the next step is a future code subproject that:

- places scenario action anchors on the authored grass-field-004 map
- maps each anchor to a matrix interaction row and a motif
- wires the anchor actions to `playtest::GrannysYardSession` legal actions
- validates each wired action against the goal model metrics

## File layout

```
subprojects/grass-field-005/
  README.md
  PLAN.md                                ← this file
  docs/
    00_puzzle_matrix_report.md           ← comprehensive report and analysis
    01_interaction_matrix.md             ← canonical consolidated matrix table
    02_axis_model.md                     ← axis vocabulary and expansion guide
    03_motif_catalog.md                  ← all motifs, fully specified
    04_goal_model.md                     ← goal stack and conflict analysis
    05_composition_guide.md              ← how to compose a full puzzle
  lessons/
    lesson_001_puzzle_matrix_fundamentals.md
```
