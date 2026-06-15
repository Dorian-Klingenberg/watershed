# Puzzle Composition Guide

This guide explains how to design a complete puzzle from scratch using the matrix
vocabulary. It covers the process step by step, with a worked example.

## Overview

A complete puzzle has four layers:

1. **Motif selection** — pick which design patterns to combine
2. **Scene mapping** — connect those patterns to actual locations on the map
3. **Goal assignment** — set the primary goals, safety constraints, and quality goals
4. **Validation** — prove the happy path and identify the trap paths

These layers build on each other. A mistake at any layer propagates forward. It is
faster to catch a compatibility problem during motif selection than after scene
mapping.

---

## Step 1: Choose the number of motifs

| Puzzle context | Motifs | Rationale |
|---|---|---|
| Tutorial beat | 1 | The player is learning the vocabulary; one lesson at a time |
| First competition round | 2 | Enough complexity to create role differentiation |
| Experienced round | 3 | Requires a working evidence board to be legible |
| Advanced round | 3+ | Reserve for when debugging tools are mature |

For the first full Granny's House Trial, use 2 motifs. Choose a third only if the
first two do not create enough decision conflict for different tester roles.

---

## Step 2: Select and check motif compatibility

Pick motifs from the catalog. Before committing, check all four compatibility
conditions:

**Condition A: Shared scene features**
Do the required features of both motifs fit in the same small yard without
requiring separate physical zones with no connection? If each motif needs its
own isolated corner, the composition does not interact — it is just two
independent puzzles placed adjacently.

**Condition B: Non-contradictory happy paths**
Can both happy paths be satisfied by the same player? If Motif A's happy path
requires blocking a route and Motif B's happy path requires using that route,
the composition is broken. Either redesign one motif's scene placement or replace
one motif.

**Condition C: Distinct failure modes**
Do the two trap paths produce meaningfully different failures? Two motifs that
both result in "basement floods" teach the same lesson twice. At least one
failure mode should be new.

**Condition D: Sequence clarity**
If one motif's setup depends on the other, document the intended sequence.
A sequence dependency is not a problem — it can create narrative arc. An
undocumented sequence dependency creates confusion.

---

## Step 3: Map motifs to the authored map

For each motif in the composition:

1. Name the specific location on the grass-field-004 map where the required
   features exist.
2. Name the specific objects at that location that correspond to each required
   feature.
3. Identify the player's starting camera position and what they can see before
   any action.

The starting view determines what looks tempting. If the player can see the loose
boulder wall immediately, the Loose-Wall Temptation motif is immediately active.
If the hidden drain entrance is not visible from the start, the Hidden Drain
Betrayal motif starts as a mystery.

**Map feature reference (grass-field-004):**

| Feature | Matrix role | Motif relevance |
|---|---|---|
| Stone canal (north) | Carrier — Sturdy stone canal | ROW-001, ROW-008 |
| Cistern gate | Source + Modifier — gate | ROW-007, MOTIF_OVER_SUCCESS_SURGE |
| Terraced garden (3 levels) | Carrier — Terrace overflow | MOTIF_TERRACE_SEQUENCE |
| Terrace overflow lips | Modifier — overflow lip | ROW-008, ROW-009 |
| Loose boulder wall (west) | Modifier — loose stones (structural) | MOTIF_LOOSE_WALL_TEMPTATION |
| Hidden drain (east terrace) | Carrier — buried conduit | MOTIF_HIDDEN_DRAIN_BETRAYAL |
| Safe swale (south) | Target — safe basin | ROW-009, MOTIF_BACKWATER_PROBLEM |
| Check dams | Modifier — weir | ROW-009 |
| Lower garden basin | Target — new lower garden | Primary goal target |
| House wall and cellar zone | Target — basement hazard | All safety goals |
| Domestic path | Target — soft hazard | Quality goal (path_mud) |

---

## Step 4: Assign goals

Using the goal model, assign:

- 2 primary goals (what must succeed)
- 2–3 safety goals (what must not fail)
- 1–2 quality goals (optional mastery layer)
- 1 discovery goal (optional, rewards the Systems Auditor)

Write each goal in the numeric metric format:

```yaml
primary_goals:
  - old_garden_hydration >= 70
  - new_garden_hydration >= 70
safety_goals:
  - basement_dampness < 30
  - boulder_wall_stability > 50
quality_goals:
  - erosion_damage < 40
discovery_goals:
  - reveal_hidden_drain
```

Check the goal conflict table for any pairs that cannot be simultaneously satisfied.
If a conflict exists, either adjust thresholds or redesign the scene placement.

---

## Step 5: Write the happy path

Write out the sequence of player actions that satisfies all primary and safety goals.
Use the puzzle beat template for each action in the sequence:

```yaml
beat:
  action: repair
  object: terrace overflow outlet (upper level)
  expected_result: overflow sequence restores; top bed fills before overflow feeds middle bed
  matrix_row: ROW-008
  sequence_before: open cistern gate
  failure_recoverable: true
```

If you cannot write a complete happy path, the puzzle does not have a valid solution.
Either the goal thresholds are too strict, the available actions are insufficient,
or the scene placement makes the required actions inaccessible.

---

## Step 6: Identify and document trap paths

For each motif, write the trap path:

```yaml
trap:
  motif: MOTIF_TERRACE_SEQUENCE
  player_action: dig shortcut from canal to lower garden
  reason_it_is_tempting: lower garden is visible; shortcut looks efficient
  failure_triggered: upper terrace stays dry; lower garden floods; erosion begins on shortcut trench
  failure_mode: erosion (partially recoverable) + garden_drought (recoverable by re-routing)
  lesson: elevation is logic; skipping a level breaks the sequence
```

A trap path is only useful if a real player would plausibly choose it. "Player
intentionally does the worst possible thing" is not a trap — it is chaos testing.
The trap should be the locally optimal choice that is globally suboptimal.

---

## Step 7: Run the validation checklist

Before treating a composition as design-complete:

- [ ] Happy path satisfies all primary goals
- [ ] Happy path satisfies all safety goals
- [ ] Happy path satisfies all quality goals (or documents acceptable violations)
- [ ] At least one trap path produces a distinct, plausible failure
- [ ] At least one failure mode is non-recoverable
- [ ] Systems Auditor approach (observe, test, act) can reach the happy path
- [ ] All motif required features are present in the named map locations
- [ ] No two motifs require contradictory player actions for their happy paths
- [ ] Sequence dependencies between beats are documented

---

## Worked example: GHT_COMPOSITION_001

### Motif selection

```yaml
puzzle_id: GHT_COMPOSITION_001
motifs:
  - MOTIF_TERRACE_SEQUENCE
  - MOTIF_LOOSE_WALL_TEMPTATION
  - MOTIF_BACKWATER_PROBLEM
```

**Why this combination works:**

- Terrace Sequencing is the primary lesson: the system has a designed order and
  the player needs to read it before routing.
- Loose-Wall Temptation is the primary trap: the obvious efficient route alongside
  the wall is dangerous and will be chosen by a Builder-role player.
- Backwater Problem is the tertiary complication: the player who correctly avoids
  the wall by blocking that corridor has now created a backwater that needs a
  relief path — discovered only after the initial "smart" action.

**Compatibility check:**

| Condition | Result |
|---|---|
| A: Shared scene features | Yes — all three motifs operate in the west-side canal + terrace zone |
| B: Non-contradictory happy paths | Yes — repair terrace outlet (Motif 5), route around wall (Motif 2), clear safe swale as relief (Motif 4) |
| C: Distinct failure modes | Yes — drought/erosion (Motif 5), wall collapse + house moisture (Motif 2), overflow at worse point (Motif 4) |
| D: Sequence clarity | Yes — Motif 5 should resolve before full flow; Motif 4 discovered after Motif 2 is addressed |

### Happy path

```yaml
sequence:
  - beat: inspect terrace overflow lips before routing
    matrix_row: ROW-015 (wait and observe)
    result: player identifies blocked upper outlet and the three-level sequence

  - beat: repair upper terrace overflow outlet
    matrix_row: ROW-008
    result: top bed receives water; overflow lip becomes active

  - beat: open cistern gate partially
    matrix_row: ROW-007 (partial)
    result: controlled flow enters stone canal; terraces begin filling top-down

  - beat: clear safe swale as relief path before blocking west corridor
    matrix_row: ROW-001 (clear weeds from swale)
    result: relief path active; surplus flow has a safe destination

  - beat: block west corridor (boulder wall side)
    matrix_row: ROW-004 (place stones in channel)
    result: flow redirected away from wall; backwater rises; safe swale accepts surplus
```

### Trap path (Builder role)

1. Player sees lower garden as target. Shortest route is alongside west wall.
2. Player digs trench alongside wall (ROW-005 on soft soil).
3. Flow starts. Wall base erodes. Wall slumps. Flow redirects south along house.
4. Boulder wall stability drops below 50% → safety goal violated.
5. House moisture begins accumulating.

**Lesson learned by trap:** the obvious route was structurally dangerous; inspecting
the wall before digging would have revealed the instability.

### Evidence board capture

After the round, the evidence board should show:

- `old_garden_hydration` and `new_garden_hydration` — pass or fail
- `basement_dampness` — rising or stable
- `boulder_wall_stability` — current value and timestamp of any drops
- `erosion_damage` — accumulated value
- Any discovery events (hidden drain found, terrace sequence read)

The host uses these facts to judge whether the Builder's path was impressive in its
speed, disastrous in its consequences, or both.
