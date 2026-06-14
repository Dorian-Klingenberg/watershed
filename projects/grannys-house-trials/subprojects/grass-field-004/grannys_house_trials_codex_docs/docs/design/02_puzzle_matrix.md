# Puzzle Matrix

The puzzle matrix is a design vocabulary for constructing water-system puzzles.

It is **not** expected to prove solvability by itself. It defines meaningful interactions. Solvability is validated by executing a known happy path through the simulation.

## Core formula

```text
player action × material/object × water behavior × consequence × lesson
```

## Interaction matrix

| Player action | Material / object | Immediate effect | Water-system result | Possible chaos | Lesson |
|---|---|---|---|---|---|
| Clear weeds | Ancient canal | Flow resistance decreases | More water reaches terraces | Overtopping if flow too high | Blockages can be accidental controls |
| Clear weeds | Dirt bank | Roots removed | Bank becomes less stable | Erosion or breach | Vegetation can stabilize terrain |
| Remove stones | Loose boulder wall | Local support weakens | Wall may slump | Flow redirects toward house | Some debris is structural |
| Place stones | Dirt channel | Roughness/capacity changes | Flow slows or diverts | Upstream backwater | Blocking flow stores energy |
| Dig canal | Hard dirt | New controlled path | Water follows created slope | Erodes if too steep | Slope matters as much as direction |
| Dig canal | Soil | Easy trench | Water spreads/absorbs | Mud and seepage | Soft ground is unreliable infrastructure |
| Open cistern | Sturdy canal | Infinite source begins | System activates | Downstream overload | Sources need control |
| Repair mechanism | Terrace overflow | Restores intended outlet | Sequential terrace watering | Wrong outlet sends flow into hazard | Ancient systems are coupled |
| Build weir/check dam | Channel | Raises upstream level | Diverts to side path | Overtops somewhere worse | Raising water changes all routes |
| Expose old drain | Buried conduit | Water disappears | May seem helpful | Basement floods from below | Hidden systems matter |

## Axis model

| Axis | Candidate values |
|---|---|
| Source | cistern, rainfall, terrace overflow, ponded water, hidden spring, old drain |
| Carrier | sturdy canal, dirt trench, grass swale, stone-lined channel, buried conduit, seepage through hill |
| Modifier | weeds, debris, loose stones, gate, overflow lip, weir, slope, basin, planter edge |
| Target | old terraced garden, new garden, safe basin, boulder wall, house wall, basement, path |
| Failure mode | overflow, erosion, seepage, collapse, blockage, backflow, hidden reroute, saturation |
| Player tool | clear, dig, place stone, remove stone, repair, open/close, test small flow, wait |

## Puzzle row template

Each generated or authored puzzle beat should be describable as:

```yaml
beat_id: string
primary_goal: string
player_action: string
object_or_material: string
intended_effect: string
water_response: string
risk: string
lesson: string
happy_path_role: required | optional | forbidden
trap_role: none | tempting | severe
```

## Important design rule

The matrix creates **recipes**, not proofs.

A matrix combination can be design-valid but geometry-invalid. The same recipe can be easy, impossible, or chaotic depending on exact elevation, capacity, distance, and hazard placement.

Use the matrix to generate puzzle intent. Use simulation to validate actual behavior.
