# Puzzle Motifs

Puzzle motifs are reusable patterns the generator can compose. A motif is a small systems problem with a lesson, a happy-path role, and one or more trap variants.

## Motif: Blockage as Control

```yaml
motif_id: MOTIF_BLOCKAGE_CONTROL
lesson: Some blockages are accidental flow controls.
happy_path: Clear only enough weeds to allow low flow.
trap_path: Clear all weeds, causing excessive flow and overtopping.
required_features:
  - weeds in canal or outlet
  - downstream capacity limit
failure_modes:
  - terrace overtops
  - soil channel erodes
  - basement dampness rises due to extra flow
```

## Motif: Loose Wall Temptation

```yaml
motif_id: MOTIF_LOOSE_WALL_TEMPTATION
lesson: The obvious channel edge may be unstable.
happy_path: Route around or reinforce boulder wall before using nearby flow.
trap_path: Dig straight beside loose boulders; water undercuts wall.
required_features:
  - loose boulder wall
  - nearby slope
  - tempting downhill corridor
failure_modes:
  - wall stability drops
  - wall collapses
  - water redirects toward house
```

## Motif: Hidden Drain Betrayal

```yaml
motif_id: MOTIF_HIDDEN_DRAIN_BETRAYAL
lesson: Old infrastructure can be connected to hidden hazards.
happy_path: Test drain with small flow or block it.
trap_path: Use drain as overflow disposal; basement floods from below.
required_features:
  - buried drain or conduit
  - basement hazard zone
  - visible excess water problem
failure_modes:
  - basement dampness increases despite no surface water touching house
```

## Motif: Backwater Problem

```yaml
motif_id: MOTIF_BACKWATER_PROBLEM
lesson: Blocking flow stores water and raises level.
happy_path: Create a relief spillway or low-flow bypass.
trap_path: Dam dangerous route completely; water pools and overtops elsewhere.
required_features:
  - choke point
  - upstream basin
  - alternate overflow route
failure_modes:
  - overtopping
  - sudden new channel
  - house-adjacent pooling
```

## Motif: Terrace Sequencing

```yaml
motif_id: MOTIF_TERRACE_SEQUENCE
lesson: Terraces should fill and overflow in order.
happy_path: Repair overflow lips and inlet sequence.
trap_path: Dig shortcut that skips upper beds or floods lower beds.
required_features:
  - at least two terrace levels
  - overflow lips
  - damaged outlet or blockage
failure_modes:
  - one bed dry, another drowned
  - too much downstream flow too soon
```

## Motif composition rule

Start with 1 motif for early tutorial puzzles. Use 2 motifs for the first full Granny's House Trial. Use 3 or more only after debugging tools are strong.

Recommended first full composition:

```yaml
puzzle_id: GHT_COMPOSITION_001
motifs:
  - MOTIF_TERRACE_SEQUENCE
  - MOTIF_LOOSE_WALL_TEMPTATION
  - MOTIF_BACKWATER_PROBLEM
```
