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
      │  ████████████┌───────┐████████████████████  ← main channel x=[47,53]
      │  ████████████│       │████████████████████
      │  █████████░░░│ (×1)  │████████████████████  ← stage 1 north tip + 1-cell junction
      │  ███████░░░░░│       │████████████████████    x=[41,46], staircase taper
      │  █████░░░░░░░│       │████████████████████
      │  ███░░░░░░░░░│       │████████████████████  ← full body x=[36,46]
      │  ███░░░░░░░░░│ (×4)  │████████████████████  ← 4-cell south scoop junction
      │  ████████████│       │████████████████████
      │  ████████████│ (×1)  │░░░░░████████████████  ← stage 2 east, 1-cell north junction
      │  ████████████│       │░░░░░░░░████████████    x=[54,64], staircase taper east
      │  ████████████│       │░░░░░░░░░░░░████████
      │  ████████████│ (×4)  │░░░░░░░░░░░░████████  ← 4-cell south scoop
      │  ████████████│       │████████████████████
      │  █████████░░░│ (×1)  │████████████████████  ← stage 3 west, 1-cell north junction
      │  ███████░░░░░│       │████████████████████    same D-shape as stage 1
      │  ███░░░░░░░░░│       │████████████████████
      │  ███░░░░░░░░░│ (×4)  │████████████████████  ← 4-cell south scoop
      │  ██████┌─────┴───────┴──────────────┐█████  ← exit pool x=[22,78], z=[88,97]
      │  ██████│           (14in)           │█████
     z=100
```

`(×1)` = 1-cell junction, `(×4)` = 4-cell junction.

| Feature | Height | Notes |
|---|---|---|
| Background | 72 in | solid |
| Source reservoir | 14 in | x=[42,58], z=[2,10] |
| Main channel | 14 in | x=[47,53], z=[10,88] |
| Stage 1 bypass (west) | 14 in | D-shape x=[36,46], z=[12,36] |
| Stage 2 bypass (east) | 14 in | D-shape x=[54,64], z=[44,66] |
| Stage 3 bypass (west) | 14 in | D-shape x=[36,46], z=[72,87] |
| Exit pool | 14 in | x=[22,78], z=[88,97] |

Each bypass is a D-shape: a staircase taper narrows the north end to
5 cells wide; the south end stays at 10 cells wide and opens via a
4-cell junction. The 4:1 junction asymmetry is the key mechanical
feature — in the hard direction the wide south scoop catches the flow
directly; in the easy direction the narrow north junction offers little
entry area.

## Junction Geometry

Stage 1 north junction (narrow, easy-direction exit):

```
x:  36    41 42 43 44 45 46 47   53
z:
12  ██████ ░░░░░░░░░░ ██│   │████  ← bypass tip 5 cells wide, no junction
13  ██████ ░░░░░░░░░░ ░░│   │████  ← junction: x=46 open (1 cell)
14  ██████ ░░░░░░░░░░ ██│   │████  ← x=46 solid again; bypass continues
```

Stage 1 south junction (wide, hard-direction scoop):

```
x:  36 37 38 39 40 41 42 43 44 45 46 47   53
z:
32  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ██│   │████  ← full bypass body, no junction
33  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ░░│   │████  ← junction row 1
34  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ░░│   │████  ← junction row 2
35  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ░░│   │████  ← junction row 3
36  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ ░░│   │████  ← junction row 4
37  ████████████████████████████ ██│   │████  ← solid below
```

`░` = 14in fluid, `█` = 72in solid.

At z=13: u-face at x=47 (between x=46 and x=47) is active — 1 opening.
At z=33..36: u-face at x=47 is active — 4 openings (4× the entry area).
At all other z in the bypass range: u-face at x=47 is inactive.

## What To Watch

- **Add water at source, Step (x100):** water fills the main channel and
  reaches the stage 1 north tip at z≈13. Watch whether more fluid enters
  through the 1-cell north junction or accumulates until the wave front
  reaches the 4-cell south scoop at z=33..36.

- **Toggle advection on, then add more water:** with the "Advection
  (u*du/dx)" checkbox enabled, add water at the source and step. Compare
  how much fills the bypass versus the main channel. With advection, the
  wave front arrives at the south scoop carrying southward momentum; the
  4-cell scoop presents a wider target and the advection term deflects
  more fluid in. The 1-cell north junction remains a narrow exit.

- **Compare easy vs. hard with advection on:** reset, add water at the
  EXIT pool instead (select z=[88,97], add water, step). The wave now
  travels north and hits stage 3's south scoop first. Note whether more
  fluid enters the bypass in the hard direction — it should, because the
  wide south scoop faces directly into the northward flow.

- **Bypass oscillation:** once a bypass fills, the trapped water
  oscillates between its north and south junctions. Watch stage 1 (west):
  the 4-cell south end oscillates visibly while the 1-cell north end
  trickles. The asymmetry in junction width creates an asymmetric
  oscillation envelope.

- **Step (x1000):** all three stages are coupled through the main channel.
  With advection enabled the waveforms look noticeably asymmetric —
  more energy has bled into the bypass bodies from the south junctions.

- **Why it still doesn't fully close:** even with advection, the valve
  only partially closes. The D-shape and asymmetric junctions bias the
  flow, but complete closure requires the bypass return flow to strongly
  oppose the main-channel flow. Our solver's advection scheme is
  first-order upwind (diffusive), which smears out the directional
  momentum. Semi-Lagrangian advection would sharpen the effect.

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
