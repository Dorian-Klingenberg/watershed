# 03 — Matrix Model

## Original interaction matrix

The original matrix is:

```text
player action × terrain/material/object × water behavior × consequence × lesson learned
```

This is the main systems vocabulary.

## Example interaction rows

| Player action | Material / object | Immediate effect | Water-system result | Possible chaos | Lesson |
|---|---|---|---|---|---|
| Clear weeds | Ancient canal | Flow increases | Terraces water faster | Overtopping if downstream not ready | Blockages can be control features |
| Clear weeds | Dirt bank | Roots removed | Bank loses reinforcement | Side erosion forms new channel | Vegetation can be structural |
| Remove stones | Loose boulder wall | Support weakens | Flow can undercut wall | Wall collapses toward house | Some debris is load-bearing |
| Place stones | Dirt channel | Flow slows | Lower erosion risk | Backwater pools upstream | Blocking flow stores energy |
| Dig canal | Hard dirt | New path forms | Water reaches lower area | Erosion if too steep | Slope matters as much as direction |
| Dig canal | Soil | Easy trench | Water spreads and absorbs | Mud/seepage near house | Soft ground is unreliable |
| Open cistern gate | Sturdy canal | Infinite water enters | Old garden activates | Over-success floods downstream | Sources need control |
| Repair mechanism | Terrace overflow | Intended sequence restored | Beds water top-down | Wrong outlet sends flow west | Ancient systems are coupled |
| Build weir | Channel | Upstream water rises | Side route activates | Overtops toward house | Raising water changes paths |
| Expose buried drain | Unknown conduit | Water disappears | May seem useful | Basement floods from below | Hidden systems matter |

## Five-axis simplified generator matrix

| Axis | Example values |
|---|---|
| Source | cistern, rainfall, terrace overflow, ponded water, hidden drain |
| Carrier | sturdy canal, dirt trench, grass swale, stone-lined channel, buried conduit |
| Modifier | weeds, debris, loose stones, planter edge, gate, weir, slope |
| Target | terraced garden, new garden, safe basin, house wall, basement, boulder wall |
| Failure mode | overflow, erosion, seepage, collapse, blockage, backflow, hidden reroute |

## What the matrix can and cannot prove

The matrix can prove compatibility and generate candidate puzzle recipes.

It cannot alone prove hydraulic solvability because solvability depends on exact geometry, elevation, flow capacity, material thresholds, available actions, hidden conduits, and action order.

Use the matrix to generate ideas. Use happy-path validation to prove at least one solution.
