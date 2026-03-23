# Experiment 004 Tutorial

This is a short, practical walkthrough for learning the voxel viewer in this folder.

If you are new, read this after [BEGINNERS_GUIDE.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/BEGINNERS_GUIDE.md).

---

## Goal

The goal of this experiment is not to "win."

The goal is to learn how to:

* read the voxel views
* understand what `?` means
* move between slice modes
* inspect buried structure
* reveal hidden material
* connect local observations to system consequences

---

## First Mental Model

The world uses this convention:

* `+Y` is up
* `XZ` is the ground plane
* lower `y` means deeper underground

The three main slice modes are:

* `5` `XY slice at z`
* `6` `YZ slice at x`
* `7` `XZ slice at y`

The easiest beginner starting point is:

* `7` `XZ slice at y`

That is the top-down / horizontal ground slice.

---

## What `?` Means

When you see `?`, it means:

* there is a voxel there
* the system thinks it is still concealed
* you do not yet have enough direct knowledge to identify its true material cleanly

It does **not** mean:

* empty space
* missing data
* error

Think of `?` as:

> "something is here, but your current understanding is incomplete."

---

## The Controls You Will Use First

### View switching

* `5` `XY slice at z`
* `6` `YZ slice at x`
* `7` `XZ slice at y`
* `8` `isometric`

### Plane movement

* `[` previous plane
* `]` next plane

### Cursor movement

* arrow keys move the sample cursor

### Peeling away layers

* `,` peel more front layers
* `.` restore peeled layers

### Interventions

* `x` excavate inspection shaft
* `p` pack terrace fracture
* `c` clear collector
* `v` vent ancestor well
* `z` reset

---

## Tutorial Walkthrough

### Step 1: Start in top-down view

Press `7`.

You are now in `XZ slice at y`.
This is the easiest view to understand first because it is the ground plane.

What to notice:

* left/right corresponds to `x`
* up/down corresponds to `z`
* the current `y` plane is shown in the status panel

Use `[` and `]` to move through different `y` heights.

What you are learning:

* high `y` planes are near the surface
* lower `y` planes are deeper underground

---

### Step 2: Inspect a visible surface layer

Stay in `XZ slice at y` and move to a high `y` plane.

Use the arrow keys to move the sample cursor.

Watch the sample panel.

What to notice:

* surface water and marsh soil are easier to identify
* shallow layers should feel more legible
* the sample panel tells you the exact `(x, y, z)` coordinate

What you are learning:

* the sample panel is your close inspection tool
* the slice itself gives pattern
* the sample panel gives specifics

---

### Step 3: Move deeper

Press `[` a few times in `XZ slice at y`.

You should move down into lower `y` planes.

What to notice:

* more voxels may appear as `?`
* deeper structure is more concealed
* you are not supposed to know everything immediately

What you are learning:

* buried structure starts hidden on purpose
* discovery is part of the experiment

---

### Step 4: Switch to a vertical slice

Press `5` for `XY slice at z`, or `6` for `YZ slice at x`.

These are vertical cross-sections.

What to notice:

* `+Y` is up in these views
* you can see the stack of layers more clearly
* this is where bad layering or buried structures become easier to spot

What you are learning:

* top-down is best for footprint and spread
* vertical slices are best for depth and layering

---

### Step 5: Peel layers away

Press `,` a few times.

This peels away front layers relative to the current view.

Then press `.` to restore them.

What to notice:

* peeling is a viewing aid
* it does not destroy the world
* it helps expose structure behind the front-most material

What you are learning:

* the viewer supports "look behind this" inspection
* this is especially useful in dense or buried areas

---

### Step 6: Use excavation

Press `x`.

This creates an inspection shaft.

What to notice:

* some previously hidden voxels should stop being mysterious
* the event log explains the consequence in character
* the sample panel becomes more informative around the shaft

What you are learning:

* excavation trades safety for knowledge
* revealing structure is an intentional action, not free information

---

### Step 7: Compare views after excavation

After pressing `x`, switch between:

* `7` top-down
* `5` vertical slice
* `6` alternate vertical slice

What to notice:

* the same change becomes more or less readable depending on the slice
* the world is one volume, but different views explain different truths about it

What you are learning:

* no single view is enough by itself
* understanding comes from comparing views

---

### Step 8: Try a systemic intervention

Press one of:

* `p`
* `c`
* `v`

Then read:

* the region metrics
* the event log
* the sample panel

What to notice:

* interventions are not just cosmetic
* they change the simulation state
* the event log gives the "human interpretation"
* the metrics give the system summary

What you are learning:

* local actions and system consequences are linked
* this experiment is about legibility and consequence, not just rendering

---

## A Good Beginner Practice Loop

If you want a repeatable way to use the tool, do this:

1. press `z` to reset
2. press `7` to go top-down
3. inspect surface planes with `[` and `]`
4. switch to `5` or `6` to inspect vertical layering
5. use `,` to peel layers
6. move the cursor onto interesting `?` regions
7. press `x` to excavate
8. compare before/after across views
9. try one intervention and watch the metrics/events

That loop teaches almost everything important in the current prototype.

---

## How To Tell If You Understand It

You probably "get" the current experiment if you can answer these:

* Which slice mode is top-down?
* What does `?` mean?
* How do you move deeper underground?
* How do you reveal buried structure without changing the whole world?
* What is the difference between peeling a view and excavating a shaft?
* Why would you use both a top-down slice and a vertical slice?

If those answers feel easy, the viewer is starting to make sense.

---

## Common Confusions

### "I see `?`, so that must be empty."

No.
It means concealed, not absent.

### "Peeling layers is the same as excavation."

No.
Peeling changes only the view.
Excavation changes the world state.

### "One view should tell me everything."

Not in this experiment.
You are expected to compare views.

### "The event log is just flavor."

Not entirely.
It is flavor, but it is also trying to tell you what system change just mattered.

---

## Where To Go Next

After you are comfortable with the viewer:

* read [BEGINNERS_GUIDE.md](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/BEGINNERS_GUIDE.md) again
* then read [simulator.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/simulator.cpp)
* then read [dashboard.cpp](/D:/Repos/Games/TheGame/experiments/voxel-infrastructure-004/dashboard.cpp)

If you want to make your first code change, good starter tasks are:

* improve a label
* add a new event line
* add a new overlay explanation
* add a new visible landmark in the world definition

---

## Short Version

Use `7` first.
Use `[` and `]` to move through `y` planes.
Use arrow keys to inspect.
Treat `?` as "concealed voxel."
Use `5` and `6` to understand depth.
Use `,` and `.` to peel the view.
Use `x` when you want the world itself to reveal more.
