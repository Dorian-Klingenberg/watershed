# 09 — Codex / Implementation Notes

## Suggested repo location

```text
docs/concepts/grannys-house-trials/
```

## Suggested early implementation modules

```text
src/granny_trials/
  terrain/
    MaterialTypes.*
    ElevationGrid.*
    TerrainEditActions.*
  water/
    WaterCell.*
    FlowSimulator.*
    SeepageModel.*
    ErosionModel.*
  objects/
    Contraption.*
    SluiceGate.*
    OverflowLip.*
    CheckDam.*
    SplitterBox.*
    HiddenDrain.*
  goals/
    Goal.*
    GoalEvaluator.*
    Meters.*
  generation/
    PuzzleRecipe.*
    PuzzleGraph.*
    PuzzleGenerator.*
    HappyPathValidator.*
  scripts/
    PlayerAction.*
    ActionScript.*
```

## Early prototype simplification

Use a 2D grid heightfield first.

Each cell can track:

- elevation
- material
- water depth
- saturation
- flow velocity proxy
- erosion resistance
- stability
- basement hazard influence
- object/contraption reference

## Materials

Initial material set:

- stone canal
- hard dirt
- soil
- lawn/grass
- loose stone/boulder
- masonry/foundation
- garden bed
- water

## Player actions

Initial action set:

- dig shallow channel
- fill channel
- place stone
- remove stone
- clear weeds
- trim/preserve roots
- clear debris/silt
- repair contraption
- open/close/tune gate
- inspect/test flow

## Early simulation rules

Keep simple and legible:

- water flows to neighboring cells with lower effective height
- effective height = elevation + water depth
- stone canal has high capacity and low erosion
- soil absorbs water and saturates
- saturated cells near house raise basement dampness
- fast flow through soil/hard dirt increases erosion
- fast flow near loose boulders reduces stability
- contraptions modify local flow rules

## Testing priority

Do not start by making a perfect water sim. Start by making the happy path testable.

The first automated test should prove:

1. Load a hand-authored Granny map.
2. Run a scripted happy path.
3. Confirm old garden watered.
4. Confirm new garden watered.
5. Confirm basement dampness below threshold.
6. Confirm boulder wall did not collapse.

Then add trap tests.
