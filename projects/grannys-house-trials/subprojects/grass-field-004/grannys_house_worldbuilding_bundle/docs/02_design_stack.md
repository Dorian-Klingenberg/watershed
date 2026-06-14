# 02 — Design Stack

Do not make one giant matrix that tries to do everything. Use a layered stack.

```text
WORLD OBJECTS
  ↓
SYSTEM INTERACTIONS
  ↓
GOALS & CONSTRAINTS
  ↓
PUZZLE MOTIFS
  ↓
HAPPY PATH VALIDATION
```

## 1. World objects

World objects are things that exist in the environment and make the place feel real.

Examples:

- cistern
- sturdy canal
- loose boulder wall
- terraced garden
- splitter box
- root strainer
- hidden drain
- rain barrel
- check dam
- old overflow lip

These are world-building first. They become mechanics later.

## 2. System interactions

System interactions describe what happens when water, terrain, tools, and objects meet.

Examples:

| Interaction | Result |
|---|---|
| fast water + loose boulders | destabilization, washout, collapse |
| weeds + canal | blockage, slowed flow, accidental filtering |
| roots + dirt bank | bank stabilization but flow obstruction |
| stone canal + water | safe high-capacity transport |
| standing water + house foundation | seepage and basement dampness |
| check dam + steep channel | lower velocity but higher upstream water level |

## 3. Goals and constraints

Goals explain why the player interacts with the system.

Examples:

- water the old terraced garden
- water the new garden
- keep basement dampness below threshold
- avoid destabilizing the boulder wall
- preserve flowers or pumpkins
- restore ancient mechanism

## 4. Puzzle motifs

Puzzle motifs are reusable small problem patterns.

Examples:

- blockage-as-control
- loose-wall temptation
- hidden drain betrayal
- backwater problem
- terrace sequencing
- over-success surge

## 5. Happy path validation

Generated puzzles do not need to prove all possible player behavior. They only need to prove at least one known scripted solution exists.

Each puzzle should carry metadata describing its known happy path. The validator runs that action script and checks goals.
