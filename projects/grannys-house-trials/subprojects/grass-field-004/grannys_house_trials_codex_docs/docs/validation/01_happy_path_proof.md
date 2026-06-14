# Happy Path Proof

## Core idea

The generator does not need to prove that all player behaviors are solvable. It only needs to prove:

> At least one known sequence of actions achieves the required goals.

That sequence is the **happy path**.

## Definition

A happy path is an action script attached to a puzzle instance. When executed in the deterministic simulation, it should satisfy all primary and safety goals.

## Example happy path

```yaml
happy_path_id: HP_GHT_001
name: Restore Terraces and Route Safe Bypass
preconditions:
  - cistern gate is closed
  - canal intake partially blocked by weeds
  - terrace overflow lip is damaged
  - safe bypass corridor exists but is not dug
  - boulder wall is unstable but not collapsed
  - basement dampness is 0
steps:
  - tick: 0
    action: clear_weeds
    target: canal_intake
  - tick: 1
    action: repair
    target: upper_terrace_overflow_lip
  - tick: 2
    action: open_gate
    target: cistern_gate
    parameters: {level: low}
  - tick: 20
    action: dig
    target: safe_bypass_segment_1
    parameters: {depth_delta: 0.2}
  - tick: 21
    action: dig
    target: safe_bypass_segment_2
    parameters: {depth_delta: 0.2}
  - tick: 22
    action: place_stone
    target: steep_bypass_section
    parameters: {count: 3, role: check_dam}
  - tick: 60
    action: wait
    parameters: {ticks: 100}
expected_metrics:
  old_garden_hydration: '>= 70'
  new_garden_hydration: '>= 70'
  basement_dampness: '< 30'
  boulder_wall_stability: '> 50'
  erosion_damage: '< 40'
```

## Validator process

1. Load puzzle.
2. Reset simulation state.
3. Apply actions at scripted ticks.
4. Run until max tick.
5. Compute metrics.
6. Compare metrics against goals.
7. Record pass/fail and event log.

## Acceptance criteria

A puzzle is accepted if:

- Happy path passes all primary goals.
- Happy path passes all safety goals.
- Happy path does not rely on hidden random outcomes.
- Happy path actions refer to valid objects/cells.
- Happy path is not absurdly precise unless it is intended as an advanced puzzle.

## Robustness testing

After first success, run perturbations:

- Slightly longer wait before opening gate.
- Slightly shorter wait before digging bypass.
- Open gate medium instead of low.
- Omit one optional stone placement.

The puzzle should not require impossible precision for tutorial levels.

## Important distinction

Happy path proof does not mean the puzzle is fair, fun, or readable. It means it is not impossible. Scoring and playtesting decide whether it is good.
