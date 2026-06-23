# CPU 15 - Tesla Valve

## Overview

This experiment places the same MAC grid solver on a map shaped like a
Tesla valve: a straight main channel with three rectangular bypass-loop
stages. Nikola Tesla patented the valve in 1920 (US1329559) as a passive
one-way fluid control with no moving parts. The question this experiment
asks is:

> Can the MAC grid show directional flow asymmetry from geometry alone,
> without an advection term?

The short answer: not meaningfully. But the experiment makes visible
exactly why, and points directly at the missing term.

## How a Tesla Valve Works

In a real fluid, each bypass loop connects to the main channel at two
points — an upper junction and a lower junction (relative to the easy
flow direction).

```
Easy direction → (south, +z)

Main channel      ┌──────────────┐
(straight) ───────┤ upper junct. │──────────
                  │              │
                  │  bypass loop │
                  │              │
           ───────┤ lower junct. ├──────────
                  └──────────────┘
```

**Easy direction (south):** the bypass entry at the lower junction faces
upstream (north). Fluid approaching from the north has to backtrack to
enter. Most stays in the main channel.

**Hard direction (north):** the bypass entry at the lower junction now
faces directly into the incoming flow. Fluid enters the loop naturally,
travels up the bypass corridor, and exits at the upper junction going
south — directly opposing the incoming hard-direction stream. The
collision raises backpressure.

This is a momentum effect. The fluid's inertia carries it into the bypass
in the hard direction. Without inertia, the fluid just distributes by
pressure gradient, which is direction-agnostic.

## Why the MAC Grid Falls Short Here

The MAC grid stores face velocities that accumulate pressure impulses
across steps, giving the simulation genuine inertia. Waves propagate,
reflections are clean, and slope acceleration is real. But the velocity
equation is:

```
∂u/∂t = -g ∂η/∂x  -  α·u
```

There is no `u · ∂u/∂x` term — no advection. Advection is what carries a
fluid parcel's existing momentum around a corner. Without it:

- A north-moving parcel arriving at the lower bypass junction has the
  junction open to the west. The pressure field will push some fluid west
  into the bypass. But there is no momentum to preferentially deflect a
  northward-moving parcel into a westward bypass the way inertia would in
  a real fluid.
- The bypass acts as a parallel path. Fluid distributes between main
  channel and bypass proportionally to conductance, which is the same in
  both directions.

The valve geometry IS built correctly. Run it anyway — the bypass loops
fill, oscillate, and interact with the main channel waves in ways that are
interesting to watch. The asymmetry you won't see is the valve closing.

## Map Layout

```
     z=0
      │  ████████████████████████████████████████  ← 72in plateau
      │  ██████┌───────────────┐████████████████  ← source reservoir
      │  ██████│   (14in)      │████████████████    x=[42,58], z=[2,10]
      │  ████████████┌───┐████████████████████████  ← main channel
      │  ████████████│   │████████████████████████    x=[47,53]
      │  ██┌─────────┘   │████████████████████████  ← stage 1 west bypass
      │  ██│  (14in) ┌───┤████████████████████████    x=[33,46], z=[16,32]
      │  ██└─────────┘   │████████████████████████    junctions at z=16,32
      │  ████████████│   └────────────────────┐███  ← stage 2 east bypass
      │  ████████████│   ┌────────────────────┘███    x=[54,66], z=[40,56]
      │  ████████████│   │████████████████████████    junctions at z=40,56
      │  ██┌─────────┘   │████████████████████████  ← stage 3 west bypass
      │  ██│         ┌───┤████████████████████████    x=[33,46], z=[64,80]
      │  ██└─────────┘   │████████████████████████    junctions at z=64,80
      │  ████████████│   │████████████████████████
      │  ██████┌─────┴───┴─────────────────┐██████  ← exit pool
      │  ██████│         (14in)             │██████    x=[22,78], z=[88,97]
     z=100
```

| Feature | Height | Notes |
|---|---|---|
| Background | 72 in | solid |
| Source reservoir | 14 in | x=[42,58], z=[2,10] |
| Main channel | 14 in | x=[47,53], z=[10,90] |
| Stage 1 bypass (west) | 14 in | x=[33,45], z=[16,32] |
| Stage 2 bypass (east) | 14 in | x=[55,66], z=[40,56] |
| Stage 3 bypass (west) | 14 in | x=[33,45], z=[64,80] |
| Exit pool | 14 in | x=[22,78], z=[88,97] |

Each bypass connects to the main channel through a single-cell junction
opening on each end. The wall between bypass corridor and main channel
(x=46 for west stages, x=54 for the east stage) is solid everywhere
except at the two junction cells.

## Junction Geometry

For Stage 1 (west bypass):

```
x:  33          45 46 47   53
z:
15  ████████████████ ██│   │████  ← all solid outside channel
16  ░░░░░░░░░░░░░░░ ░░│   │████  ← junction row: corridor + x=46 open
17  ░░░░░░░░░░░░░░░ ██│   │████  ← x=46 solid (wall), corridor continues
 ⋮      bypass      ██│   │████
31  ░░░░░░░░░░░░░░░ ██│   │████  ← x=46 solid
32  ░░░░░░░░░░░░░░░ ░░│   │████  ← junction row: corridor + x=46 open
33  ████████████████ ██│   │████  ← solid again
```

`░` = 14in fluid, `█` = 72in solid.

At z=16 and z=32: u-face at x=47 (between x=46 and x=47) is active.
At z=17..31: u-face at x=47 is inactive — wall seals the corridor off.

## What To Watch

- **Add water at source, Step (x100):** water fills the main channel and
  reaches stage 1 at z=16. Watch the junction cell light up as a small
  amount of fluid enters the bypass corridor. The bypass fills slowly
  while the main channel wave continues south.

- **Bypass oscillation:** once water is in the bypass corridor and the
  main channel wave has passed, the trapped bypass water oscillates
  back and forth (bouncing between the two junction cells). The period
  depends on corridor length — longer than the main channel wave period
  at the same depth.

- **Compare easy vs. hard:** add water at the source (easy direction,
  south). Note how much water ends up in each bypass corridor. Then
  reset, add water at the EXIT pool instead (hard direction, north —
  select a cell in z=[88,97] first). Observe whether the bypass corridors
  fill differently. In a model with advection they would. Here the
  difference will be small.

- **Step (x1000):** jumps to fully-developed multi-stage wave interaction.
  By this point every bypass corridor has some water and all three stages
  are coupling through the main channel.

- **Why it doesn't fully valve:** the bypass corridors drain and fill
  symmetrically because there is no term to preferentially carry momentum
  around the corner at the junction. The fluid at the junction sees a
  pressure gradient going into the bypass but no extra nudge from its
  existing velocity direction. Adding `u · ∇u` is the fix.

## Implementation

```cpp
m_sims.push_back(std::make_unique<sim::SimpleSloshMacSim>(
    "Tesla Valve",
    sim::SeedMapProfile::TeslaValve,
    50, 6,       // default brush: center of source reservoir
    16.0f));     // 16in fills
```

The terrain builder `build_tesla_valve_seed()` in `main.cpp` uses a
`carve()` helper to cut corridors and `set()` to open individual junction
cells, keeping the wall between each bypass and the main channel solid at
all non-junction z-rows.

## Files

| File | Purpose |
|---|---|
| `sim/simple_slosh_mac_sim.h` | MAC solver — unchanged |
| `sim/i_field_sim.h` | `SeedMapProfile::TeslaValve` added |
| `main.cpp` | `build_tesla_valve_seed()`, CPU 15 registration, lesson entry |
| `lesson_experiment_tesla_valve.md` | This lesson |

## Takeaway

The Tesla valve map makes the missing advection term concrete. The
geometry is correct. The junctions are correctly isolated. The bypass
corridors fill and oscillate. But the valve does not close, because
closing requires a fluid parcel to *carry its direction of travel* into
the junction and use that to preferentially enter the bypass. That is
`u · ∇u`. Adding it is the natural next step for this branch.
