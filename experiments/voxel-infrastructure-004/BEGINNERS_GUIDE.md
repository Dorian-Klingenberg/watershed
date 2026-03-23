# Beginner's Guide to Experiment 004

This file is the friendly starting point for working with the voxel experiment in this folder.

When future work happens inside `experiments/voxel-infrastructure-004`, this guide should be kept updated so it continues to match the actual controls, files, and architecture direction.

---

## What This Experiment Is

This is a small voxel-based prototype for exploring one core idea:

> can we use a voxel world to inspect hidden infrastructure, environmental fields, and repair consequences in a readable way?

It is not the final engine.
It is not full gameplay.
It is an experiment for learning what kind of voxel tools and simulation patterns are useful for the larger game.

The scenario is:

* a marsh settlement sits above buried ancient infrastructure
* some of that infrastructure is hidden at first
* the player can inspect the volume from different directions
* the player can intervene
* local improvements may create downstream problems

This fits the larger project themes:

* hidden structure
* partial understanding
* intervention under uncertainty
* unintended consequences

---

## Important Reference Files

If you are new, start with these files in this order:

1. [BEGINNERS_GUIDE.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/BEGINNERS_GUIDE.md)
2. [TUTORIAL.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/TUTORIAL.md)
3. [voxel_simulation_engine_core_architecture_context_v0_2.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/voxel_simulation_engine_core_architecture_context_v0_2.md)
4. [simulator.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.h)
5. [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
6. [dashboard.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.h)
7. [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)
8. [main.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/main.cpp)

The architecture context file is the long-term design reference for this experiment line and future sub-experiments in this directory.

---

## Folder Layout

This folder currently contains:

* [CMakeLists.txt](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/CMakeLists.txt)
* [main.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/main.cpp)
* [simulator.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.h)
* [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
* [dashboard.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.h)
* [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)
* [world_definition.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.h)
* [world_definition.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.cpp)
* [voxel_simulation_engine_core_architecture_context_v0_2.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/voxel_simulation_engine_core_architecture_context_v0_2.md)
* [BEGINNERS_GUIDE.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/BEGINNERS_GUIDE.md)
* [TUTORIAL.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/TUTORIAL.md)
* [subexperiments/voxel-d3d12-001/README.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/subexperiments/voxel-d3d12-001/README.md)
* [subexperiments/voxel-d3d12-001/main.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/subexperiments/voxel-d3d12-001/main.cpp)

Think of those files like this:

* `main.cpp`: starts the experiment
* `simulator.*`: owns the world data and updates it over time
* `dashboard.*`: draws the terminal UI and handles controls
* `world_definition.*`: shared static world layout used by the console experiment and graphics-side sub-experiments
* `CMakeLists.txt`: tells CMake how to build this experiment
* `voxel_simulation_engine_core_architecture_context_v0_2.md`: bigger-picture architecture direction
* `BEGINNERS_GUIDE.md`: onboarding and usage guide
* `TUTORIAL.md`: hands-on walkthrough
* `subexperiments/voxel-d3d12-001/*`: first Direct3D 12 visualization pass for the same world

---

## The Big Mental Model

If you only want one simple explanation, use this:

1. We store a small 3D voxel world.
2. Some underground structure starts hidden.
3. The simulator updates a few environmental values over time.
4. Those values are reflected back into voxel properties like wetness, pressure, and stress.
5. The dashboard lets you inspect the world from different directions.
6. You can peel away front layers to reveal what is behind them.
7. You can trigger interventions and observe tradeoffs.

That is the current core loop.

One important coordinate-system note:

* `x` and `z` are horizontal world-space directions across the terrain
* `y` is world-space up

So higher `y` means higher elevation.
Buried infrastructure lives at lower `y`, and the surface lives near the top of the `y` range.

This is the current preferred world-space convention for this experiment line and future sub-experiments in this directory:

* `+Y` is up
* `XZ` is the horizontal ground plane
* lower `y` means deeper underground

If later work changes that convention, this file should be updated immediately so it remains the persistent source of truth.

---

## How the Program Starts

The entry point is [main.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/main.cpp).

Its job is very small:

* parse command-line options
* create dashboard options
* start the dashboard
* print an error if the experiment crashes

The real work happens in:

* [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
* [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)

---

## How the Simulation Is Structured

The main simulation types live in [simulator.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.h).

The most important ones are:

### `MaterialKind`

This says what a voxel is made of.

Examples:

* air
* surface water
* marsh soil
* terrace fill
* bedrock
* ancient conduit
* delay basin
* collector
* inspection shaft

### `VoxelState`

This is the data stored for each voxel.

Right now each voxel can hold:

* material type
* saturation
* pressure
* salinity
* stress
* hidden flag
* exposed flag

This is an early example of the broader "voxel as simulation substrate" idea.

### `RegionMetrics`

These are higher-level system values for the whole scenario.

Right now they include:

* `upland_head`
* `leak_flux`
* `marsh_depth`
* `orchard_supply`
* `settlement_stability`
* `conduit_integrity`
* `collector_clearance`
* `spirit_whisper`

Think of these as the current simplified simulation summary.

### `SimulationSnapshot`

This is the package of data the UI reads.

It includes:

* current tick
* world dimensions
* current viewed plane
* all voxel data
* region metrics
* recent event messages

### `Simulator`

This is the main simulation class.

Its main public functions are:

* `reset()`
* `step()`
* `apply_intervention(...)`
* `set_viewed_layer(...)`
* `snapshot()`

---

## How the World Is Built

The initial world is created in `build_world_definition()` inside [world_definition.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.cpp).

The console simulator reads that shared definition during `reset()` in [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp).

The world dimensions are currently:

* width = 14
* height = 8
* depth = 5

In this experiment, those map to world axes like this:

* `width` -> `x`
* `height` -> `y`
* `depth` -> `z`

And importantly:

* `y` is up
* `y = height - 1` is the surface/top of the current toy world
* smaller `y` values are deeper underground

Very roughly:

* top `y` layer includes air and surface water
* the next layer down includes marsh soil and terrace fill
* the middle layers are mostly terrace fill
* buried conduits and basins sit inside the lower terrace / foundation zone
* only the deepest layers are bedrock

The buried features include:

* an ancient conduit
* a delay basin
* a collector

Those underground features are deliberately hidden at first.

That matters because this project is about discovery, not immediate full knowledge.

---

## How a Simulation Tick Works

Each time the experiment advances, `step()` is called in [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp).

It currently does two main things:

1. `update_hydrology()`
2. `update_voxels()`

### `update_hydrology()`

This updates the scenario-level values.

Examples:

* how much leakage is happening
* how deep the marsh has become
* how much water reaches the orchard
* how stable the settlement is
* how much shortcut/spirit influence is present

This is still a simplified model.
It is more like "environmental consequence logic" than a full physical simulation.

### `update_voxels()`

This pushes those bigger values back into individual voxel states.

Examples:

* marsh surface voxels get wetter
* conduit voxels show more pressure
* buried damaged areas show more stress
* some hidden structure becomes exposed under certain conditions

This is the key bridge between:

* system-level simulation values
* local spatial inspection

---

## Interventions

The experiment currently supports four interventions.

These are triggered from the UI and handled in `apply_intervention(...)` in [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp).

### `x` Excavate inspection shaft

What it does:

* reveals a vertical slice
* marks those voxels as exposed
* slightly reduces settlement stability

Why it exists:

* knowledge is useful
* knowledge can also have cost

### `p` Pack terrace fracture

What it does:

* improves conduit integrity
* reduces leak flux

Why it exists:

* local repair can reduce marsh damage
* downstream effects may still remain

### `c` Clear collector spillway

What it does:

* improves collector clearance
* improves orchard supply

Why it exists:

* downstream benefits matter
* local and regional interests can diverge

### `v` Vent ancestor well

What it does:

* increases spirit influence
* increases pressure/head
* may reveal more hidden structure indirectly

Why it exists:

* this connects the voxel prototype to the larger slow-path vs shortcut-path design

---

## How the Viewer Works

The UI code lives mostly in [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp).

The dashboard does three main jobs:

1. draw a view of the voxel world
2. show a side panel of metrics and events
3. handle keyboard input

The voxel-view panel is now kept at a consistent display size across slice modes and isometric mode, so the screen layout should stay more stable while you switch views.
It also shows axis labels to make the current world orientation easier to interpret.

The recent-events panel is intentionally a little more atmospheric than a normal debug log.
It is meant to read like a sarcastic field journal from an overqualified, underslept expert who is annoyed that he has to keep helping you personally.
Even with that tone, the events should still describe real simulation changes clearly enough to be useful.

### Overlays

The four overlays are:

* `1` material
* `2` saturation
* `3` pressure
* `4` structural risk

These are different ways of looking at the same world.

When you see `?` in the material view, it means:

* there is a voxel there
* the experiment considers its true material still concealed
* the concealment may come from burial, lack of excavation, or incomplete understanding

So `?` does not mean "empty."
It means "present, but not yet properly identified."

### Camera / View Modes

The current viewer supports four inspection modes:

* `5` XY slice at `z`
* `6` YZ slice at `x`
* `7` XZ slice at `y`
* `8` crude isometric

These are not full 3D camera systems.
They are inspection views for learning and debugging.

This wording is important:

* `XY slice at z` means you are looking at the XY plane while choosing which `z` layer to inspect
* `YZ slice at x` means you are looking at the YZ plane while choosing which `x` layer to inspect
* `XZ slice at y` means you are looking at the XZ plane while choosing which `y` layer to inspect

Because `y` is world up:

* `XY slice at z` is a vertical slice
* `YZ slice at x` is a vertical slice
* `XZ slice at y` is a horizontal slice

This is clearer than the older wording like "looking along z," which can be easy to misread.

The viewer also includes axis hints inside the voxel panel itself, such as:

* short axis labels placed above, below, left, and right of the view
* the active slice plane noted near the bottom label
* direction implied by label placement rather than long explanatory sentences

The rendered slice content is also centered inside a fixed-size canvas so the panel does not jump around as much when you change views.
Arrow-key movement is intended to follow the visible screen directions, so pressing up should move the sample upward in the current displayed view.
The viewer should no longer be upside down: positive direction on the displayed vertical slice axis should appear higher on screen, not lower.
The three slice views should now be internally consistent about positive directions. The crude isometric view is only an approximate perspective aid, so treat its labels as orientation hints rather than exact screen-axis guarantees.

### Plane Selection

Use:

* `[` previous plane
* `]` next plane

The meaning of "plane" depends on the current view:

* in `XY slice at z`, choose which `z` layer you are slicing
* in `YZ slice at x`, choose which `x` layer you are slicing
* in `XZ slice at y`, choose which `y` layer you are slicing
* in isometric, plane value matters less for drawing, but the selected voxel is still tracked

Plane stepping now follows the full valid range of the active axis:

* `z` slices can step through all `z` planes
* `x` slices can step through all `x` planes
* `y` slices can step through all `y` planes

So the active plane is no longer incorrectly limited by the `z` depth when you are viewing `x` or `y` slices.

### Hiding Front Layers

Use:

* `,` hide more front layers
* `.` restore hidden layers

This is the "peel away voxels to see behind them" feature.

It works relative to the current inspection mode:

* in `XY slice at z`, it hides front `z` layers
* in `YZ slice at x`, it hides front `x` layers
* in `XZ slice at y`, it hides front `y` layers
* in isometric, it hides a crude front wedge

This is currently a simple viewer tool.
It does not delete voxels from the simulation.
It only changes what the viewer shows.

### Sample Cursor

Use the arrow keys to move the sample cursor.

The cursor is used to inspect one voxel at a time.
The side panel shows:

* voxel position
* material
* saturation
* pressure
* stress
* salinity
* hidden/exposed status

The cursor moves differently depending on the current camera mode so it stays meaningful within that view.

---

## Current Controls

### Simulation

* `space` play/pause
* `n` single-step
* `-` slower
* `+` faster

### Overlays

* `1` material
* `2` saturation
* `3` pressure
* `4` structural risk

### Camera

* `5` XY slice at `z`
* `6` YZ slice at `x`
* `7` XZ slice at `y`
* `8` crude isometric

### View Manipulation

* `[` previous plane
* `]` next plane
* `,` hide more front layers
* `.` restore hidden layers
* arrow keys move the sample cursor

### Actions

* `x` excavate inspection shaft
* `p` pack terrace fracture
* `c` clear collector spillway
* `v` vent ancestor well
* `z` reset scenario
* `q` quit

---

## How to Think About the Code

If you are new to code, the easiest way to read this experiment is:

1. read the type definitions in [simulator.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.h)
2. read [world_definition.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/world_definition.cpp) to see the shared world layout
3. read `reset()` in [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
4. read `step()`, `update_hydrology()`, and `update_voxels()` in [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
5. read the UI state and enums in [dashboard.h](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.h)
6. read `handle_keypress(...)` in [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)
7. read `draw_frame(...)` and `draw_slice(...)` in [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)

That order works because it goes from:

* data
* to simulation
* to controls
* to rendering

instead of jumping around.

---

## What Is Simplified Right Now

This experiment is intentionally early and simplified.

It does not yet have:

* chunk streaming
* GPU working-set management
* visible-face extraction
* a real renderer
* real field propagation between neighboring voxels
* persistent dirty-region infrastructure
* save/load

There is now also one graphics-side sub-experiment:

* [subexperiments/voxel-d3d12-001](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/subexperiments/voxel-d3d12-001)

That D3D12 viewer is still intentionally small.
It is a way to orbit around the current visible voxel mass with the mouse, not a replacement for the simulation dashboard.

Right now it is best understood as:

* a spatial inspection prototype
* a simulation sketch
* a viewer for thinking about hidden structure

That is still useful.
It helps us figure out what future engine features are actually worth building.

---

## Relationship to the Architecture Context

The architecture document in [voxel_simulation_engine_core_architecture_context_v0_2.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/voxel_simulation_engine_core_architecture_context_v0_2.md) is broader than this experiment.

This experiment currently touches a few of those ideas in early form:

* voxels as the shared spatial substrate
* scalar-like per-voxel values
* hidden structure inside a voxel volume
* persistent state rather than pure one-frame visuals
* multiple kinds of spatial inspection

But it does not yet implement the bigger architecture pieces like:

* CPU authoritative chunk streaming
* GPU memoized working set
* derived-cache structures
* additive accumulated process fields like `erosionDebt`
* dirty-region uploads

So think of this experiment as a stepping stone, not the finished architecture.

---

## How to Run It

This repo is set up with CMake, and the root project includes this experiment through [CMakeLists.txt](/D:/Repos/Games/TheGame/CMakeLists.txt).

The intended executable name is:

* `voxel_infrastructure_004_experiment`

Typical command-line options from [main.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/main.cpp):

* `--ticks N`
* `--sleep-ms N`
* `--no-clear`
* `--non-interactive`

Example idea:

```powershell
voxel_infrastructure_004_experiment --ticks 120 --sleep-ms 100
```

At the moment, there is a local MSVC environment issue in this workspace where standard headers like `<string>` and `<array>` are not being found during compile.
That appears to affect older experiments too, so it looks like a workspace/toolchain problem rather than a problem specific to this folder.

Because of that, this guide documents intended usage even if the local toolchain needs fixing before the executable can actually be built here.

---

## Good First Code Changes for a Beginner

If you want safe first tasks in this directory, good options are:

* add a new material kind
* add a new overlay
* tweak event text
* add one more intervention
* improve the crude isometric projection
* add a second hidden infrastructure feature
* add a new metric to the side panel

These are easier than jumping straight into a full engine rewrite.

---

## What Should Be Kept Updated

When work happens in this directory, this file should be updated if any of these change:

* controls
* file list
* major simulation concepts
* interventions
* overlays
* camera modes
* build/run instructions
* architecture references used by the experiment line

If the experiment grows into sub-experiments in this same directory, this guide should stay the first onboarding document and point to any new files or branches of work.

---

## Short Summary

Experiment 004 is a beginner-friendly voxel inspection prototype for hidden infrastructure and environmental consequence.

Right now it is mainly about:

* spatial inspection
* buried structure
* multiple views
* peel-away visibility
* simple interventions
* consequence-oriented thinking

It is a useful early step toward a larger voxel simulation engine, but it is still intentionally small and readable.
