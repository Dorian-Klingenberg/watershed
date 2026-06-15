# Goal Model

Goals sit above the puzzle matrix. The matrix describes what interactions are
meaningful. Goals describe what the player is trying to accomplish, how success
is judged, and where tensions live.

## The goal stack

Each puzzle has multiple goal layers. They are evaluated from the top down. A
puzzle cannot be accepted if its primary goals are impossible to satisfy together,
or if a safety goal is violated by the only path that satisfies the primary goals.

| Type | Purpose | Visible to player? |
|---|---|---|
| Story | Human motivation — why does this matter to Granny? | Yes |
| Primary | The win condition — what the player must achieve | Yes |
| Safety | A constraint the player must not violate | Yes (ideally) |
| Stability | System integrity — a slow-degrading limit | Usually yes |
| Quality | Mastery layer — how well, not just whether | Optional |
| Discovery | A hidden world fact the player can uncover | Not until discovered |
| Optional | Flavor — extra credit that adds replayability | Yes |
| Comedy/failure | A memorable wrong answer — what Chaos Tester might achieve | Yes (after the round) |

## Numeric metrics

Use numeric metrics during development even if the final game presents them
diegetically. Concrete thresholds make it possible to write a happy-path
simulation that proves the puzzle is solvable.

| Metric | Description | First threshold |
|---|---|---|
| `old_garden_hydration` | Percent of old terraced garden adequately watered | >= 70% |
| `new_garden_hydration` | Percent of lower garden adequately watered | >= 70% |
| `basement_dampness` | Accumulated moisture/seepage risk under house | < 30% |
| `boulder_wall_stability` | Remaining structural integrity of loose rock wall | > 50% |
| `erosion_damage` | Accumulated terrain surface damage | < 40% |
| `path_mud` | Water or mud on domestic path | < 20% (soft limit) |
| `flow_control` | Percent of water reaching intended routes vs. total released | > 60% (quality) |

## Primary prototype goal package

```yaml
goal_package_id: GHT_GOAL_001
name: Water New Garden Safely

story_goal: >
  Granny has extended the garden to the lower terrace and needs the ancient
  irrigation system working again before the dry season.

primary_goals:
  - old_garden_hydration >= 70
  - new_garden_hydration >= 70

safety_goals:
  - basement_dampness < 30
  - boulder_wall_stability > 50  # no catastrophic collapse

quality_goals:
  - erosion_damage < 40
  - path_mud < 20

optional_goals:
  - save the flowers (flower_bed_intact == true)
  - do not drown pumpkins (pumpkin_bed_flood < 10%)
  - keep water visibly moving through ancient canal at all times

discovery_goals:
  - reveal_hidden_drain: player discovers the drain before using it
  - identify_terrace_sequence: player reads all overflow lips before routing

failure_goals:
  - basement_flood is possible if player digs straight downhill and opens full flow
  - boulder_wall_washout is possible if fast water undercuts loose rocks
  - garden_drought is possible if player blocks too many flow paths
```

## Goal conflict table

Understanding conflicts prevents designing a puzzle with no valid solution.

| Goal A | Conflicting pressure | Resolution path |
|---|---|---|
| Deliver water quickly | Fast flow causes erosion and wall failure | Open gate partially; increase flow gradually |
| Clear all blockages | Some weeds stabilize and slow flow | Identify which blockages are controls first |
| Use shortest downhill route | Short route threatens house foundation | Route around; reinforce; or slow the flow |
| Open cistern fully | Infinite source overwhelms downstream | Open partially; monitor; close when goals met |
| Use hidden drain | Drain connects to basement moisture zone | Test with small flow first |
| Protect boulder wall | Routing around wall may use dangerous soil | Reinforce or use check dam upstream |
| Maximize garden hydration | High hydration requires high flow; high flow erodes | Find a stable flow rate that satisfies threshold |
| Achieve fast round completion | Quick actions skip discovery goals | Discovery goals are optional — this is a valid trade |

## Goal object model

```text
Goal
  id              string
  name            string
  type            story | primary | safety | stability | quality | discovery | optional | failure
  metric_name     string
  comparison      >= | > | <= | < | == | !=
  threshold       number
  priority        integer (1 = highest; lower priority goals yield when conflicts arise)
  visible_to_player   bool
  fail_fast       bool  (if true, round ends immediately on violation)
  recoverable     bool  (if true, player can still fix the situation after violation begins)
  description     string
```

## Acceptance criteria for a generated puzzle

A generated or authored puzzle is only acceptable if:

1. The known happy path satisfies all primary goals and all safety goals simultaneously.
2. At least one tempting trap path violates a safety or quality goal in an
   understandable way (the player can see why they were wrong in retrospect).
3. At least one failure mode is non-recoverable (not all mistakes can be undone).
4. A Systems Auditor-style approach (observe before acting, test before committing)
   can reach a complete solution without triggering any safety goal violation.

If criterion 4 fails, the puzzle is unfair. A patient, methodical approach should
always have a path to success even if it is not the fastest one.

## How goal type shapes design decisions

**Story goals** shape the scripted context. They tell the player why to care. They
do not constrain the solution. A story goal that says "Granny wants her garden
watered" is satisfied by any path that achieves the primary goal. Story goals should
not introduce additional mechanical constraints.

**Safety goals** are what create tension. A puzzle with primary goals but no safety
goals is just a task — route water here, done. Safety goals are what make the player
think before they act. Every puzzle should have at least one.

**Quality goals** are what separate a clean solve from a perfect solve. They are the
layer that rewards the Systems Auditor and creates the scoring conversation between
tester and host. A player who achieves primary and safety goals but violates a
quality goal still wins the round but does not get full points.

**Discovery goals** are the world-building layer. They do not change water routing
but they change the player's understanding of the place. A hidden drain is just a
hazard until the player discovers it. After discovery, it becomes a world fact —
something Granny's yard has always had, now revealed. Hosts should reward discovery
separately from routing success.

**Comedy/failure goals** are the Chaos Tester's specialty. They should be possible
outcomes, not designed traps. The basement flood should be achievable through
plausible-but-wrong decisions, not through a deliberately invisible tripwire.
