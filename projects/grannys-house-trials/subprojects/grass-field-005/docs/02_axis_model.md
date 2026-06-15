# Axis Model

The axis model defines the combinatorial space that the interaction matrix draws
from. Every row in the matrix is a specific combination of values across these axes.
Understanding the axes makes it possible to identify gaps in the matrix, generate
new rows intentionally, and check whether a proposed puzzle beat is coherent.

## The six axes

### Axis 1: Source

Where water originates. The source determines baseline flow rate, whether flow is
controllable, and whether it is finite or infinite.

| Value | Characteristics |
|---|---|
| Cistern | Infinite source once opened; requires a gate action to start |
| Rainfall | Pulsed, uncontrollable; arrives from above onto all surfaces equally |
| Terrace overflow | Derived source; only active when an upstream terrace is overfull |
| Ponded water | Finite, gravity-driven; drains in one direction unless blocked |
| Hidden spring | Continuous low-flow; may not be obvious until player investigates |
| Old drain (reverse) | A drain that accepts water also discharges it somewhere else |

**Design note:** mixing sources adds complexity fast. The first tutorial should use
one source. A competition round can use two if both are easily distinguishable.

### Axis 2: Carrier

How water travels from source to target. The carrier determines capacity, fragility,
and how easy it is for the player to observe the flow.

| Value | Characteristics |
|---|---|
| Sturdy stone canal | High capacity, stable, visible; can still overtop |
| Dirt trench (player-dug) | Low capacity, fragile; erodes under fast flow |
| Grass swale | Slow, distributed; naturally filters but absorbs water |
| Stone-lined channel (ancient) | Moderate capacity, stable but may have blocked sections |
| Buried conduit | Unknown capacity; cannot be observed directly without exposure |
| Seepage through hill | Very slow, silent; creates wet zones at unpredictable distances |
| Terrace-to-terrace overflow | Capacity = overflow lip height; precision matters |

**Design note:** the carrier is where most player mistakes happen. Players naturally
choose the easiest carrier (a new dirt trench) without checking whether it can
handle the source flow.

### Axis 3: Modifier

What changes the carrier's behavior. Modifiers are the primary targets for player
action — most player tools act on modifiers, not on the carrier itself.

| Value | Effect on carrier |
|---|---|
| Weeds / roots | Reduce flow rate; can be removed |
| Debris / stones | Reduce capacity; can be removed or added |
| Gate or sluice | Binary control of flow; can be opened or closed |
| Overflow lip | Defines maximum water level before diversion; can be repaired or raised |
| Weir | Raises upstream level; diverts excess to side path |
| Slope angle | Determines flow speed; cannot be changed easily |
| Basin or planter edge | Accumulates water; releases slowly or not at all |
| Loose stones (structural) | Appear removable but are load-bearing; removal causes collapse |
| Roots (structural) | Appear to be plant matter but hold bank together |

**Design note:** modifiers that look removable but are structural are the source
of the most interesting traps. The player's mental model is "clearing = better";
the game's truth is "some blockage is intentional control."

### Axis 4: Target

Where water arrives — intended or not. A puzzle has at least one desired target
and at least one hazard target.

| Value | Is it a hazard? | Notes |
|---|---|---|
| Old terraced garden | No | The primary historic watering destination |
| New lower garden | No | The primary new-construction watering destination |
| Safe basin | No | Designated overflow/emergency storage |
| Safe swale | No | Designed overflow path that does not reach hazards |
| Boulder wall base | Yes | Water here undercuts wall stability |
| House wall | Yes | Moisture accumulates; leads to basement dampness |
| Basement / cellar | Yes | Catastrophic; hard to reverse |
| Main domestic path | Soft hazard | Mud; annoyance/scoring penalty |
| Flower beds | Optional | Loss = quality penalty only |

**Design note:** a puzzle needs at least one good target and one hazard target that
are plausibly reachable from the same source. If the hazard target is geographically
impossible to reach, there is no tension.

### Axis 5: Failure mode

How things go wrong. Failure modes are what happens when the player sends water
somewhere it should not go, or when they apply too much or too little change.

| Value | Recoverable? | Observable? |
|---|---|---|
| Overflow | Yes | Yes — visible pooling |
| Erosion | Partially | Yes — terrain changes |
| Seepage | No | Slow — only visible downstream |
| Collapse | No | Yes — sudden |
| Blockage | Yes | Yes — flow stops |
| Backflow | Yes | Usually — upstream pooling |
| Hidden reroute | No | No — water disappears |
| Saturation | Partially | Slow — soil darkens, becomes soft |

**Design note:** at least one failure mode in a puzzle should be non-obvious and
non-recoverable. That is what makes the "hidden dependency" feel meaningful. If
every mistake can be undone, the round becomes repetitive trial-and-error rather
than genuine investigation.

### Axis 6: Player tool

What the player can physically do. This is the thinnest axis — it needs expansion
as the game adds more agency.

| Value | Acts on |
|---|---|
| Clear (weeds, debris) | Modifier |
| Dig (new channel) | Carrier |
| Place stone | Modifier |
| Remove stone | Modifier |
| Repair mechanism | Overflow lip, gate, outlet |
| Open / close gate | Modifier (gate) |
| Build weir or dam | Modifier |
| Redirect flow (at junction) | Carrier |
| Plug outlet | Modifier (overflow lip) |
| Test small flow | Source (partial open) |
| Wait and observe | None |
| Expose buried conduit | Carrier (buried) → Carrier (visible) |

**Design note:** "wait and observe" and "test small flow" are the two tools that
reward patience. Puzzles should have at least one situation where the optimal play
is to watch before acting. This distinguishes the Systems Auditor tester role from
the Builder and Chaos Tester roles.

## How to use the axis model to generate new rows

To generate a new matrix row:

1. Pick a **player tool** from Axis 6.
2. Pick a **modifier** from Axis 3 (the tool's natural target).
3. Identify the **carrier** the modifier is on (Axis 2).
4. Ask: what does the carrier connect to? That gives the **target** (Axis 4).
5. Ask: what source feeds the carrier? That gives the **source** (Axis 1).
6. Ask: what goes wrong if the player misjudges? That gives the **failure mode** (Axis 5).
7. Write the causal chain as a row in the interaction table.

If you cannot complete step 6 with a plausible failure mode, the row is not
interesting enough to include. The lesson is "nothing bad happens," and that does
not earn a matrix row.

## How to check if a proposed row is coherent

A row is coherent if:

- the player tool is applicable to the named material or object
- the immediate effect follows directly from applying that tool to that material
- the water-system result follows from the immediate effect without invented steps
- the possible chaos is a realistic escalation of the water-system result
- the lesson is specific enough to be teachable (not just "be careful")

A row that requires "and then somehow" between any two columns is not coherent.
Either the gap is a missing row (there is an intermediate step that deserves its
own row) or the combination is not actually meaningful.
