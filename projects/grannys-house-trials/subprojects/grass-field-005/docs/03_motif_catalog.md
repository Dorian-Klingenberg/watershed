# Motif Catalog

A motif is a named, reusable design pattern. It is a small systems problem with a
lesson, a happy path, at least one trap path, and required features.

Motifs are composed to create full puzzles. A tutorial uses one. The first full
competition round uses two or three.

## Motif 1: Blockage-as-Control

```yaml
motif_id: MOTIF_BLOCKAGE_CONTROL
lesson: Some blockages are accidental flow controls — removing them all makes things worse.
happy_path: Clear only enough weeds or debris to allow a controlled low flow.
trap_path: Clear everything completely; flow exceeds downstream capacity; system overtops.
matrix_rows: [ROW-001, ROW-004]
```

**Required scene features:**
- Weeds, debris, or loose stones partially blocking a canal or outlet
- A downstream capacity limit that is exceeded by full unblocked flow
- No other obvious flow path visible to the player

**Failure modes triggered by trap path:**
- Terrace overtopping → soil erosion or basement seepage
- Soil channel erosion → uncontrolled new path forming
- Basement dampness rising from increased flow volume

**How to set this up in the map:**
Place weeds at a choke point in the stone canal, between the cistern outlet and the
terrace inlet. The blocked flow rate keeps the first terrace at safe capacity. Full
clearing pushes past the terrace overflow lip. The player cannot see the overflow
lip from the starting camera angle — they only see the weed blockage.

**Tester role that hits the trap:** Builder (clears everything in preparation)

**Tester role that avoids the trap:** Systems Auditor (tests flow rate before clearing)

---

## Motif 2: Loose-Wall Temptation

```yaml
motif_id: MOTIF_LOOSE_WALL_TEMPTATION
lesson: The obvious route may be structurally unsupported — temporary success is not stable success.
happy_path: Route water around the boulder wall, or reinforce it before using the adjacent corridor.
trap_path: Dig straight beside or through loose boulders; water undercuts the wall; collapse redirects flow toward house.
matrix_rows: [ROW-003, ROW-005]
```

**Required scene features:**
- Loose boulder wall adjacent to a tempting downhill corridor
- The corridor is visually the shortest path to a desired target
- The wall sits between the corridor and a hazard target (house wall, cellar)

**Failure modes triggered by trap path:**
- Boulder wall stability drops → wall slumps
- Slumped wall redirects active flow toward house foundation
- House-wall moisture exposure → basement dampness begins accumulating

**How to set this up in the map:**
The loose boulder wall on the west edge of the yard runs north-south. The lower
garden is to the south. The direct path from the cistern to the lower garden runs
alongside the wall. Digging a trench beside the wall looks efficient. Water erodes
the base; the wall falls west into the path; the re-directed flow follows the house
wall south.

**Tester role that hits the trap:** Builder (optimizes for shortest route)

**Tester role that avoids the trap:** Systems Auditor (inspects the wall before digging)

---

## Motif 3: Hidden Drain Betrayal

```yaml
motif_id: MOTIF_HIDDEN_DRAIN_BETRAYAL
lesson: Old infrastructure can be connected to hidden hazards — disappearing water is not proof of success.
happy_path: Test the drain with a small flow to identify its destination; block it or route carefully.
trap_path: Use the drain as an overflow dump; basement floods from below with no surface warning.
matrix_rows: [ROW-010, ROW-014, ROW-016]
```

**Required scene features:**
- A buried conduit or drain in a plausible overflow location
- A basement or cellar hazard zone not adjacent to the drain entrance
- At least one scenario where the player has excess water they want to dispose of

**Failure modes triggered by trap path:**
- Basement dampness rises despite no visible surface water near the house
- Player cannot identify cause because the drain entrance is remote from the hazard
- Failure is non-recoverable once basement threshold is exceeded

**How to set this up in the map:**
The old buried drain enters the ground near the east terrace wall. Its exit is under
the house foundation, feeding a cellar moisture zone. The drain is visible if the
player inspects the terrace wall closely. It is easy to miss if the player is focused
on active flow routing. Sending excess overflow into the drain solves the visible
surface problem and creates a hidden one.

**Tester role that hits the trap:** Chaos Tester (improvises disposal of excess water)

**Tester role that avoids the trap:** Systems Auditor (tests with small flow first per ROW-016)

---

## Motif 4: Backwater Problem

```yaml
motif_id: MOTIF_BACKWATER_PROBLEM
lesson: Blocking a dangerous route stores water and raises the upstream level — every dam needs a relief path.
happy_path: Create a relief spillway or low-flow bypass before blocking the dangerous route.
trap_path: Block the dangerous route completely with no relief; water pools and overtops at a worse location.
matrix_rows: [ROW-009, ROW-018, ROW-004]
```

**Required scene features:**
- A choke point or hazardous route the player wants to block
- An upstream basin or collection zone that fills when the route is blocked
- At least one alternative overflow route, initially not obvious

**Failure modes triggered by trap path:**
- Upstream pooling → overflow at a point adjacent to a hazard
- Sudden new channel forms along the path of least resistance
- House-adjacent pooling accumulates before the player notices

**How to set this up in the map:**
The player sees the loose-wall corridor as a threat (from Motif 2) and decides to
completely dam the channel feeding it with stones. That blocks the dangerous path
but raises the upstream level past the safe swale, which overtops into the domestic
path and then toward the house. The safe swale was intended as the relief path, but
the player did not know to clear it first.

**Tester role that hits the trap:** Builder (dams the danger then moves on)

**Tester role that avoids the trap:** Systems Auditor (traces all downstream paths before blocking)

---

## Motif 5: Terrace Sequencing

```yaml
motif_id: MOTIF_TERRACE_SEQUENCE
lesson: Terraces should fill and overflow in order — elevation is logic; overflow lips encode sequence.
happy_path: Repair overflow lips and inlet sequence; let upper terraces fill before lower beds receive flow.
trap_path: Dig a shortcut that skips upper beds or sends full flow to lower beds before they are ready.
matrix_rows: [ROW-008, ROW-012, ROW-005]
```

**Required scene features:**
- At least two terrace levels with defined overflow lips
- A damaged or blocked outlet on the upper terrace
- A visually appealing shortcut route that bypasses the upper level

**Failure modes triggered by trap path:**
- Upper bed stays dry; lower bed drowns
- Too much downstream flow arrives too fast; erosion begins
- Cascade failure: lower overflow overtops into a hazard before upper bed is established

**How to set this up in the map:**
The terraced garden has three levels. The top terrace outlet is blocked with debris
from an old collapse. The player can see the lower beds and a clear slope from the
canal. The temptation is to dig straight to the middle terrace and skip the repair.
That runs fast flow into the middle terrace, which overtops before the lower terrace
outlet is active, drowning everything below and leaving the top bed dry.

**Tester role that hits the trap:** Builder (creates the most efficient path to visible target)

**Tester role that avoids the trap:** Systems Auditor (reads the overflow lip sequence before acting)

---

## Motif 6: Over-Success Surge

```yaml
motif_id: MOTIF_OVER_SUCCESS_SURGE
lesson: Flow rate matters — more water is not always better; success metrics need safety constraints.
happy_path: Open the cistern gate partially; maintain a controlled flow rate; close when goals are met.
trap_path: Open the cistern gate fully; the system works briefly then overloads downstream routes.
matrix_rows: [ROW-007, ROW-013]
```

**Required scene features:**
- Cistern gate as an accessible, early control point
- Downstream system with a maximum safe capacity lower than full-flow rate
- Success metric that is satisfied before overload occurs — briefly

**Failure modes triggered by trap path:**
- Garden waters → then drowns → then overloads adjacent routes
- Downstream carrier erodes → new unplanned path forms
- Excess flow reaches boulder wall or house before player can respond

**How to set this up in the map:**
The cistern gate is the first thing the player sees and the most obvious action.
Opening it fully achieves the primary goal (garden hydration rises above threshold)
for about thirty seconds. Then the terrace overtops, the erosion field activates
on the dirt channels, and the boulder wall base starts taking water. The player
has achieved the goal and failed two safety goals simultaneously.

**Tester role that hits the trap:** Chaos Tester (opens everything immediately)

**Tester role that avoids the trap:** Systems Auditor (opens gate partially and monitors)

---

## Motif 7: Useful Danger

```yaml
motif_id: MOTIF_USEFUL_DANGER
lesson: A contraption can solve one problem and create another — know what you are trading.
happy_path: Use the contraption and manage the side effect deliberately.
trap_path: Use the contraption without awareness of its side effect; both problems now exist.
matrix_rows: [ROW-009, ROW-017, ROW-018]
```

**This motif is a meta-motif.** It does not specify one interaction but describes
a structural pattern: any tool or repair that has a useful primary effect and a
harmful secondary effect. The other motifs are specific instances of useful danger.
Use this motif as a design check: after composing a puzzle, ask whether each
contraption has exactly one useful effect and at least one side effect that matters.

**Examples of useful danger contraptions:**

| Contraption | Useful effect | Side effect |
|---|---|---|
| Check dam | Slows erosion in channel | Creates backwater; raises level upstream |
| Weir | Activates safe side channel | Raises water level near house foundation |
| Root strainer | Filters debris from flow | Blocks flow when full; creates new backup |
| Flume (plank bridge) | Bypasses soft soil | Leaks under high flow; degrades over time |
| Partial weed clearing | Maintains controlled flow rate | Leaves visible mess; may tempt further clearing |

**How to use this motif in composition:**
When composing a puzzle from two or three motifs, check each physical object in
the puzzle description against the useful danger template. Any object that has only
a useful effect (no side effect) is too safe. Add a plausible side effect or replace
the object with one that has inherent tension.

---

## Motif composition rules

| Puzzle type | Number of motifs | Notes |
|---|---|---|
| Tutorial beat | 1 | One lesson, one trap, no layering |
| First competition round | 2 | Motifs should share at least one required feature |
| Full competition round | 3 | Add only after debugging tools are strong |
| Advanced round | 3+ | Requires a fully working evidence board |

**Recommended first full composition:**

```yaml
puzzle_id: GHT_COMPOSITION_001
motifs:
  - MOTIF_TERRACE_SEQUENCE    # primary lesson: read the system before routing
  - MOTIF_LOOSE_WALL_TEMPTATION  # secondary trap: obvious route is dangerous
  - MOTIF_BACKWATER_PROBLEM   # tertiary complexity: blocking danger creates new danger
```

This composition works because:
- All three motifs share the same physical space (the west-side corridor and canal)
- The traps do not activate simultaneously — they cascade in a predictable order
- A careful player (Systems Auditor) can solve all three; a hasty player (Builder)
  will trigger at least two

**Compatibility check for motif combinations:**

Before committing to a multi-motif composition, verify:

1. Do the required scene features overlap enough to coexist in a small yard?
2. Can the happy paths of both motifs be satisfied without contradicting each other?
3. Do the trap paths of both motifs produce distinct failure modes? (Two motifs with
   the same failure mode collapse into one lesson.)
4. Is there a sequence? If one motif's trap path is a prerequisite for the other's,
   the composition has an intended order. Document it.
