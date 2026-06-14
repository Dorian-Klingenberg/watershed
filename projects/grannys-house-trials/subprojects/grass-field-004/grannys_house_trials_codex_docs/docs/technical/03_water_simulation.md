# Water Simulation — First Prototype

This is not a real fluid simulation. It is a deterministic toy model designed for puzzle validation.

## Key principle

The water solver should be simple enough to debug by printing numbers.

## Cell water height

Effective water surface height:

```text
surface_height = elevation + water
```

Water flows from a cell to lower neighboring cells if their effective surface height is lower.

Use 4-neighbor flow first: north, south, east, west. Add diagonals later only if necessary.

## Step algorithm

For each simulation tick:

1. Apply source inflows, such as cistern gate.
2. Compute candidate flows between neighboring cells.
3. Apply limited water transfers.
4. Apply absorption/saturation.
5. Apply seepage into basement hazard if applicable.
6. Apply erosion/stability changes.
7. Update hydration metrics.
8. Record events.

## Pseudocode

```text
step_simulation(grid):
    add_source_water()

    planned_transfers = []
    for cell in grid.cells:
        if cell.water <= min_water_threshold:
            continue
        neighbors = lower_neighbors(cell)
        distribute available flow among neighbors based on drop and roughness
        append transfers

    apply_transfers(planned_transfers)
    apply_absorption()
    apply_seepage()
    apply_erosion()
    apply_stability()
    evaluate_feature_metrics()
```

## Flow amount

Start with:

```text
drop = source_surface - target_surface
flow = min(source.water, drop * flow_rate / average_roughness)
```

Clamp flow to avoid numerical chaos.

Suggested starting constants:

```yaml
min_water_threshold: 0.01
base_flow_rate: 0.25
max_flow_per_edge_per_tick: 0.50
canal_flow_multiplier: 2.00
grass_flow_multiplier: 0.60
soil_flow_multiplier: 0.80
loose_boulder_flow_multiplier: 0.70
```

## Source behavior

The cistern is infinite, but gate-controlled.

```yaml
cistern_gate:
  closed: 0.0 water/tick
  low: 0.5 water/tick
  medium: 1.0 water/tick
  high: 2.0 water/tick
```

For the first prototype, allow `open_gate` to set a discrete flow level.

## Absorption and saturation

Soil and garden cells absorb water.

```text
absorbed = min(cell.water, material.absorption_rate)
cell.water -= absorbed
cell.saturation += absorbed
```

Saturation slowly decays if no water is present.

Garden hydration is computed from saturation across garden cells.

Too much saturation can count as drowning later.

## Basement seepage

The basement is under the whole house, but the north and west foundation walls are hill-embedded and more vulnerable.

For first prototype:

- Any water on cells tagged `HOUSE_ADJACENT_NORTH` or `HOUSE_ADJACENT_WEST` increases basement dampness.
- Saturated cells adjacent to those walls also increase dampness.
- Hidden drains can directly increase dampness if water enters them.

```text
if cell has tag HOUSE_SEEPAGE_RISK:
    basement_dampness += cell.water * seepage_multiplier
    basement_dampness += cell.saturation * saturation_seepage_multiplier
```

## Boulder wall stability

Loose boulder wall cells have a `stability` value.

Stability decreases when:

- water flows through or adjacent to them with high flow rate
- player removes supporting stones
- player digs below/next to them
- slope below them becomes too steep

```text
if flow_through_boulders > threshold:
    stability -= flow_through_boulders * undercut_factor
```

If stability drops below collapse threshold:

- convert some boulder cells to loose rubble/debris
- lower or raise adjacent cells depending on desired simplification
- create a new uncontrolled flow path
- emit `BOULDER_WALL_COLLAPSE` event

## Erosion

For first prototype, erosion can be simple:

```text
if material is diggable and flow > erosion_threshold:
    elevation -= erosion_rate
    erosion_damage += erosion_rate
```

Do not let erosion create unbounded terrain destruction in the first version. Clamp total elevation changes.

## Event log

Record important changes:

- `GATE_OPENED`
- `TERRACE_WATERED`
- `NEW_GARDEN_WATERED`
- `BASEMENT_DAMPENED`
- `BASEMENT_FLOODED`
- `BOULDER_WALL_UNSTABLE`
- `BOULDER_WALL_COLLAPSED`
- `EROSION_STARTED`
- `HIDDEN_DRAIN_ACTIVATED`

Events are critical for debugging and for later LLM-readable summaries.
