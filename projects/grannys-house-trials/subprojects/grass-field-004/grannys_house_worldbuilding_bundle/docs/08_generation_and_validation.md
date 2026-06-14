# 08 — Pseudo-Random Puzzle Generation & Happy-Path Validation

## Recommended generator architecture

Use a hybrid:

```text
puzzle graph / graph grammar
  → constraint-based placement
  → optional WFC/local tile fill
  → water simulation
  → happy-path validation
  → scoring/rejection
```

WFC is useful for local visual coherence and tile adjacency, but it is not enough to prove hydraulic solvability.

## Generate backwards from the happy path

Do not generate arbitrary terrain and hope it becomes a puzzle. Generate a known solution skeleton first.

Example happy path:

```text
1. Clear weeds from old canal intake.
2. Repair two missing terrace overflow stones.
3. Open cistern gate to low flow.
4. Let upper terrace fill.
5. Let overflow water lower terraces.
6. Dig shallow swale around safe side of house, outside basement danger zone.
7. Place stones at steep section as check dams.
8. Let water reach new garden.
```

Then build terrain and hazards around that known route.

## Puzzle generation loop

```text
GeneratePuzzle(seed):
    choose primary goal package
    choose safety/quality/discovery goals
    choose 2-4 required mechanics
    choose 1-3 puzzle motifs

    build abstract puzzle graph
    assign north-to-south elevations
    place cistern, house, old garden, new garden
    place required happy path route
    place hazards and tempting wrong routes
    place modifiable obstacles and contraptions

    convert graph to terrain/tile/voxel layout
    fill local detail using adjacency rules or WFC
    place vegetation, debris, paths, stones, garden elements

    run baseline simulation
    run scripted happy path validation

    if happy path fails:
        mutate/repair/reject

    run trap/failure scripts
    if no interesting failures:
        add/mutate hazards

    score readability, stability, chaos, and tutorial value
    return accepted puzzle + proof metadata
```

## Acceptance criteria

Required:

- at least one known happy path succeeds
- new garden can be watered
- old terraced garden can be watered
- basement can remain below dampness threshold on happy path
- boulder wall can remain above stability threshold on happy path

Preferred:

- one or more tempting wrong routes exist
- basement flooding is possible but avoidable
- failures are recoverable
- player can visually infer cause and effect
- multiple tools/actions matter

## Puzzle metadata example

```json
{
  "puzzleId": "GHT-0042",
  "primaryGoal": "Water the new garden",
  "safetyGoals": [
    "Keep basement dampness below 30%",
    "Keep boulder wall stability above 60%"
  ],
  "selectedMotifs": [
    "terrace_sequencing",
    "loose_wall_temptation",
    "hidden_drain_betrayal"
  ],
  "knownHappyPath": [
    "clear_canal_intake_weeds",
    "repair_upper_terrace_overflow",
    "open_cistern_low_flow",
    "dig_safe_swale_west_of_house_outside_foundation_zone",
    "place_three_check_dam_stones",
    "wait_until_new_garden_hydration_70"
  ],
  "expectedResult": {
    "newGardenHydrationMin": 70,
    "basementDampnessMax": 15,
    "boulderWallStabilityMin": 80
  },
  "designedTrap": "Direct downhill trench undercuts loose boulder wall and redirects water toward Granny's north foundation."
}
```
