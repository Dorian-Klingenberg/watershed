# Puzzle Generation Algorithm

## Main conclusion

Use a hybrid generator:

```text
puzzle matrix + goal package + motif composition + happy path generation + simulation validation
```

Wave Function Collapse may be useful later for terrain prettiness and local adjacency, but it should not be the first core generator.

## Why not pure WFC

WFC answers:

> What tiles are allowed to sit next to each other?

The puzzle generator needs to answer:

> What player-understandable sequence of actions can transform this system from unsafe/unwatered to safe/watered?

That is a planning and validation problem, not just a tile adjacency problem.

## Recommended first generator

Generate backwards from a known solution.

### Step 1: Choose goal package

Example:

```yaml
goal_package: GHT_GOAL_001
primary: water old garden and new garden
safety: keep basement dampness below 30
stability: keep boulder wall above 50
```

### Step 2: Choose motifs

Example:

```yaml
motifs:
  - MOTIF_TERRACE_SEQUENCE
  - MOTIF_LOOSE_WALL_TEMPTATION
  - MOTIF_BACKWATER_PROBLEM
```

### Step 3: Generate happy path graph

Abstract graph:

```text
CISTERN -> STURDY_CANAL -> UPPER_TERRACE -> LOWER_TERRACE -> SAFE_BYPASS -> NEW_GARDEN
```

### Step 4: Generate hazard graph

```text
LOWER_TERRACE -> TEMPTING_SHORTCUT -> LOOSE_BOULDER_WALL -> HOUSE_NORTH_WALL -> BASEMENT
```

### Step 5: Assign spatial layout

Rules:

- Cistern north/top.
- Terraces below cistern.
- House below terraces.
- New garden south/lower than old garden.
- Basement hazard under whole house.
- North/west walls of house adjacent to hill/higher terrain.
- Safe bypass should exist around house, not through seepage zone.
- Tempting route should be shorter/more obvious than safe route.

### Step 6: Convert graph to grid

Place required cells and features. Fill surrounding terrain with materials.

### Step 7: Add local terrain details

Use simple local rules first:

- stone canal prefers connected stone canal neighbors
- terraces are flat or nearly flat shelves
- boulders appear on slopes
- soil/garden appears in beds
- house occupies rectangular footprint
- paths connect house and gardens

WFC can be inserted here later.

### Step 8: Attach happy path script

The generated puzzle carries a known solution.

Example:

```yaml
happy_path:
  - clear weeds at canal intake
  - repair upper terrace overflow
  - open cistern gate to low flow
  - wait 20 ticks
  - dig shallow swale along safe corridor
  - place stones at steep section
  - wait until new garden hydration >= 70
```

### Step 9: Validate

Run simulation with happy path. Accept only if goals pass.

### Step 10: Validate trap path

Run one or more known bad scripts. Accept if at least one trap produces legible failure without immediately making the level unrecoverable.

## Generator pseudocode

```text
GeneratePuzzle(seed):
    rng = Random(seed)
    goal_package = choose_goal_package(rng)
    motifs = choose_motifs(goal_package, rng)

    happy_graph = build_happy_path_graph(goal_package, motifs, rng)
    hazard_graphs = build_hazard_graphs(goal_package, motifs, rng)

    layout = place_required_features(happy_graph, hazard_graphs, rng)
    grid = rasterize_layout(layout)
    fill_materials(grid, rng)
    add_obstacles_and_affordances(grid, motifs, rng)

    happy_script = instantiate_happy_path_actions(layout, motifs)
    trap_scripts = instantiate_trap_actions(layout, motifs)

    result = simulate(grid, happy_script)
    if not goals_pass(result.metrics):
        return reject_or_mutate(seed)

    trap_results = [simulate(grid, s) for s in trap_scripts]
    if not has_interesting_trap(trap_results):
        return mutate_hazards(seed)

    score = score_puzzle(result, trap_results, layout)
    if score < threshold:
        return reject_or_mutate(seed)

    return Puzzle(grid, goals, happy_script, trap_scripts, metadata)
```

## Scoring

Reward:

- happy path succeeds
- target is not accidentally watered before player action
- basement is not accidentally safe from all mistakes
- tempting shortcut exists
- trap is understandable
- failure is recoverable
- multiple player tools are meaningful
- terrain reads clearly from top-down view

Penalize:

- no solution
- trivial solution
- instant catastrophic failure
- water behavior too opaque
- all routes equally bad
- hidden drain causes unavoidable failure

## Future WFC integration

After graph-based generation works, WFC can decorate or vary local terrain.

Use WFC for:

- stone canal shapes
- garden bed outlines
- natural terrain textures
- ruin fragments
- vegetation clusters
- cottage surroundings

Do not use WFC to decide whether the puzzle is solvable.
