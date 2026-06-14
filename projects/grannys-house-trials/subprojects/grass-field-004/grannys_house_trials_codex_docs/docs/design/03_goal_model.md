# Goal Model

Goals sit above the puzzle matrix. The matrix describes what can happen. Goals describe what the player is trying to accomplish and how success is judged.

## Goal stack

Each puzzle should have multiple goal layers.

| Goal type | Example | Purpose |
|---|---|---|
| Story goal | Help Granny water the garden | Human motivation |
| Primary mechanical goal | Deliver water to new garden | Clear win condition |
| Safety goal | Keep basement dampness below threshold | Constraint and tension |
| Stability goal | Keep boulder wall from collapsing | System integrity |
| Quality goal | Minimize erosion and crop damage | Mastery |
| Discovery goal | Learn what old mechanism does | World mystery |
| Optional goal | Avoid muddying path / save flowers | Flavor and replayability |
| Comedy/failure goal | Allow basement flood | Memorable consequences |

## Prototype metrics

Use numeric metrics even if final game presents them diegetically.

| Metric | Description | Suggested first threshold |
|---|---|---|
| `old_garden_hydration` | Percent of old terraced garden adequately watered | >= 70% |
| `new_garden_hydration` | Percent of lower garden adequately watered | >= 70% |
| `basement_dampness` | Accumulated water/seepage risk under house | < 30% |
| `boulder_wall_stability` | Remaining stability of loose rock wall | > 50% |
| `erosion_damage` | Accumulated terrain damage | < 40% |
| `path_mud` | Mud or water on domestic path | Optional penalty |
| `flow_control` | Percent of water reaching intended routes | Optional score |

## Primary prototype goal package

```yaml
goal_package_id: GHT_GOAL_001
name: Water New Garden Safely
story_goal: Help Granny extend water to the lower new garden.
primary_goals:
  - old_garden_hydration >= 70
  - new_garden_hydration >= 70
safety_goals:
  - basement_dampness < 30
  - no catastrophic boulder wall collapse
quality_goals:
  - erosion_damage < 40
  - avoid flooding main dirt path
optional_goals:
  - save flowers
  - do not drown pumpkins
  - keep water visibly moving through ancient canal for tutorial clarity
failure_goals:
  - basement flood is possible if player digs straight downhill or opens full flow too early
  - boulder wall washout is possible if fast water undercuts loose rocks
```

## Goal conflict examples

| Goal A | Conflicting pressure |
|---|---|
| Deliver water quickly | Fast flow causes erosion and wall failure |
| Clear all blockages | Some weeds slow and stabilize flow |
| Use shortest downhill route | Short route threatens house foundation |
| Open cistern fully | Infinite source overwhelms downstream features |
| Use hidden drain | Drain may connect to basement |

## Goal object model

```text
Goal
- id
- name
- type: story | primary | safety | quality | discovery | optional | failure
- metric_name
- comparison: >= | > | <= | < | == | !=
- threshold
- priority
- visible_to_player: bool
- fail_fast: bool
- description
```

## Why goals matter to generation

A generated puzzle is acceptable only if:

1. The known happy path satisfies all primary and safety goals.
2. At least one tempting trap path violates a safety or quality goal in an understandable way.
3. Failure is recoverable unless the puzzle is explicitly configured as a hard challenge.
