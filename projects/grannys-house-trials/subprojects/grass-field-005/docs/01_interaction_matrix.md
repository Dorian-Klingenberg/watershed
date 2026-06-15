# Interaction Matrix — Canonical Form

This is the single authoritative version of the puzzle matrix interaction table.
It reconciles the codex docs version and the worldbuilding bundle version, uses
the codex docs as the format baseline, and adds rows for important combinations
not yet named.

## Core formula

```
player action × material/object × water behavior × consequence × lesson
```

## Canonical interaction table

| ID | Player action | Material / object | Immediate effect | Water-system result | Possible chaos | Lesson |
|---|---|---|---|---|---|---|
| ROW-001 | Clear weeds | Ancient canal | Flow resistance decreases | More water reaches terraces faster | Overtopping if downstream not ready | Blockages can be accidental controls |
| ROW-002 | Clear weeds | Dirt bank | Roots removed | Bank loses reinforcement | Side erosion forms a new unplanned channel | Vegetation can be structural |
| ROW-003 | Remove stones | Loose boulder wall | Local support weakens | Water can undercut wall foundation | Wall slumps or collapses; flow redirects toward house | Some debris is load-bearing |
| ROW-004 | Place stones | Dirt channel | Roughness increases, capacity decreases | Flow slows, more water absorbed nearby | Backwater pools upstream | Blocking flow stores energy and volume |
| ROW-005 | Dig canal | Hard dirt | New controlled path forms | Water follows created slope | Erosion if slope too steep; debris downstream | Slope matters as much as direction |
| ROW-006 | Dig canal | Soft soil | Easy trench forms | Water spreads and absorbs into ground | Mud and seepage near house or cellar | Soft ground is unreliable infrastructure |
| ROW-007 | Open cistern gate | Sturdy canal | Infinite-source flow begins | Entire downstream system activates | Downstream overload if not routed correctly | Sources need proportional control |
| ROW-008 | Repair mechanism | Terrace overflow outlet | Intended overflow sequence restores | Beds water top-down in designed order | Wrong outlet sends flow into hazard zone | Ancient systems are coupled and precise |
| ROW-009 | Build weir or check dam | Channel | Upstream water level rises | Water diverts to adjacent side path | Overtopping somewhere worse than original route | Raising water changes all downstream paths |
| ROW-010 | Expose buried drain | Unknown conduit | Water disappears underground | May seem useful; actual route unknown | Basement floods from below with no surface warning | Hidden systems matter; test before trusting |
| ROW-011 | Place stones | Canal inlet | Flow rate reduces | Upstream backwater, downstream dry spell | Garden under-watered; pressure builds | Every control point affects everything upstream |
| ROW-012 | Dig canal | Canal bed | Deepens existing channel | Flow capacity increases | May draw water away from side terraces | Deepening changes which outlets activate |
| ROW-013 | Open cistern gate | Dirt trench | High-volume flow enters weak carrier | Trench erodes quickly | Trench collapses; water finds random path | Carrier must match source strength |
| ROW-014 | Clear weeds | Buried conduit entrance | Conduit becomes accessible | Flow rate through conduit increases | Conduit destinations unknown; risk multiplies | Clearing one thing can activate another |
| ROW-015 | Wait and observe | Any active system | No physical change | Water reaches steady state | Reveals which routes are active at current flow | Observation is a valid action |
| ROW-016 | Test small flow | New canal or conduit | Minimal water released | Actual flow path becomes visible at low risk | Small flow may still trigger irreversible erosion | Test first, open fully second |
| ROW-017 | Redirect flow | Stone channel junction | Water diverts to chosen branch | Alternate target receives water | Primary target goes dry; may over-water alternate | Junctions are decision points, not just connectors |
| ROW-018 | Plug outlet | Active overflow lip | Overflow stops | Upstream level rises | New overflow point activates elsewhere | Every plug creates a pressure debt elsewhere |

## Row format reference

Each row captures a full causal chain:

| Column | Meaning |
|---|---|
| ID | Unique row identifier for cross-referencing in motif specs and beat templates |
| Player action | The verb — what the player does |
| Material / object | What the action is applied to |
| Immediate effect | The direct physical consequence (no water yet) |
| Water-system result | How water behavior changes as a result |
| Possible chaos | What goes wrong if the player misjudges the situation |
| Lesson | What a successful or failed attempt teaches the player |

## Puzzle beat template

Each authored or generated puzzle beat should be expressible as:

```yaml
beat_id: string
primary_goal: string
player_action: string          # matches "Player action" column
object_or_material: string     # matches "Material / object" column
intended_effect: string        # matches "Water-system result" column
water_response: string         # specific to this geometry/flow level
risk: string                   # specific to this geometry/flow level
lesson: string                 # matches "Lesson" column
matrix_row_ref: ROW-NNN        # reference to canonical row above
happy_path_role: required | optional | forbidden
trap_role: none | tempting | severe
sequence_before: beat_id | none   # must this beat come before another?
sequence_after: beat_id | none    # must this beat come after another?
failure_recoverable: true | false
```

## Version notes

This table supersedes both the codex docs version
(`grass-field-004/grannys_house_trials_codex_docs/docs/design/02_puzzle_matrix.md`)
and the worldbuilding bundle version
(`grass-field-004/grannys_house_worldbuilding_bundle/docs/03_matrix_model.md`).

Those files remain as historical context. When there was a conflict between
the two versions, the wording that made the causal chain most explicit was chosen.
Rows ROW-011 through ROW-018 are new additions not present in either source.
