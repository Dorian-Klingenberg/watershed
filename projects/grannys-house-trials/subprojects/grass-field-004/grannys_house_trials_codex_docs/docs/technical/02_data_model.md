# Data Model

## Grid coordinates

Use a 2D grid for first prototype.

Convention:

- `x` increases east/right.
- `y` increases south/downhill.
- North/top of map has lower `y`.
- Terrain generally slopes from north to south.

## Cell

```yaml
Cell:
  x: int
  y: int
  elevation: float
  material: MaterialId
  water: float
  saturation: float
  stability: float
  feature_ids: [string]
  tags: [string]
```

## Material

```yaml
Material:
  id: string
  display_name: string
  diggable: bool
  base_roughness: float
  absorption_rate: float
  seepage_rate: float
  erosion_resistance: float
  max_stable_slope: float
  water_capacity_modifier: float
  supports_channel: bool
```

Suggested starting materials:

| Material | Diggable | Absorption | Erosion resistance | Notes |
|---|---:|---:|---:|---|
| `STONE_CANAL` | no | 0.00 | 1.00 | Safe, reliable, high capacity |
| `HARD_DIRT` | yes | 0.05 | 0.60 | Can form channels |
| `SOIL` | yes | 0.15 | 0.30 | Absorbs, muddies, erodes |
| `LAWN` | yes | 0.10 | 0.45 | Slows shallow flow |
| `LOOSE_BOULDERS` | partially | 0.02 | 0.25 | Stability hazard |
| `HOUSE_FOUNDATION` | no | 0.00 | 1.00 | Boundary with seepage adjacency rules |
| `BASEMENT` | no | 0.00 | 1.00 | Hazard metric, not normal terrain |
| `GARDEN_SOIL` | yes | 0.20 | 0.25 | Hydration target, can drown |

## Feature

Features are objects or semantic zones layered on cells.

```yaml
Feature:
  id: string
  type: string
  cells: [{x, y}]
  properties: object
```

Feature types:

- `CISTERN_SOURCE`
- `GATE`
- `STURDY_CANAL`
- `TERRACE_BED`
- `OVERFLOW_LIP`
- `GRANNYS_HOUSE`
- `BASEMENT_HAZARD_ZONE`
- `BOULDER_WALL`
- `NEW_GARDEN`
- `WEEDS`
- `DEBRIS`
- `HIDDEN_DRAIN`
- `DIRT_PATH`
- `SAFE_BASIN`

## Action

```yaml
Action:
  id: string
  type: clear_weeds | dig | place_stone | remove_stone | repair | open_gate | close_gate | wait
  target: {x: int, y: int} | feature_id
  parameters: object
```

## Puzzle

```yaml
Puzzle:
  id: string
  name: string
  seed: int
  grid_width: int
  grid_height: int
  rule_version: string
  materials: [Material]
  cells: [Cell]
  features: [Feature]
  goals: [Goal]
  happy_paths: [ActionScript]
  trap_scripts: [ActionScript]
  metadata: object
```

## ActionScript

```yaml
ActionScript:
  id: string
  name: string
  description: string
  actions:
    - tick: int
      action: Action
```

## Metrics

```yaml
Metrics:
  old_garden_hydration: float
  new_garden_hydration: float
  basement_dampness: float
  boulder_wall_stability: float
  erosion_damage: float
  path_mud: float
  total_water_released: float
  total_water_lost_to_sink: float
```
