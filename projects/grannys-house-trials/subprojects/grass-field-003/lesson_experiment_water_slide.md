# CPU 14 - Water Slide

## Overview

This experiment uses the same MAC grid solver as CPU 13 with a different
terrain map: a carved slide channel descending 38 inches from a raised
reservoir to a landing pool. The question it explores is distinct from the
slosh basin:

> What does gravitational acceleration look like when momentum is properly
> preserved across steps?

In a pipe model the answer is unsatisfying — water slows down before it gets
anywhere. In the MAC grid, the face velocities accumulate every pressure
impulse the slope delivers, so the water arrives at the pool carrying genuine
speed built up over the full length of the slide.

## Map Layout

The terrain is carved out of a solid 72-inch plateau. Several regions are fluid:

```
     z=0
      │  ████████████████████████████████████████  ← 72in plateau
      │  ██████┌─────────────────┐████████████████  ← reservoir (52in floor)
      │  ██████│  TOP RESERVOIR  │████████████████    x=[42,58], z=[2,15]
      │  ██████│     (52in)      │████████████████
      │  ██████└────────┬────────┘████████████████
      │  ██████████████ │ ████████████████████████  ← slide channel (7 cells wide)
      │  ████████████┌──┴──┐████████████████████████  x=[47,53], z=[15,55]
      │  ████████████│  ↓  │████████████████████████  floor: 52→14in
      │  ████████████│  ↓  │████████████████████████
      │  ██████┌─────┴─────┴─────────────────┐██████  ← landing pool (14in)
      │  ██████│                             │██████    x=[22,78], z=[53,73]
      │  ██████│         ███████████         │██████  ← solid pillar (72in)
      │  ██████│         ███████████         │██████    x=[46,54], z=[62,67]
      │  ██████│  ████████         ████████  │██████  ← baffles (20in)
      │  ██████│  ████████  [gap]  ████████  │██████    x=[22,44] and [56,78]
      │  ██████│                             │██████    z=[68,70]
      │  ██████╠═════════════════════════════╣██████  ← lip (18in), z=74
      │  ██████│       BACK POOL (14in)      │██████    x=[22,78], z=[75,86]
      │  ██████└─────────────────────────────┘██████
      │  ████████████████████████████████████████  ← 72in plateau
     z=100
```

| Feature | Height | Dimensions |
|---|---|---|
| Background | 72 in | entire field (solid) |
| Top reservoir | 52 in | 17×14 cells, x=[42,58], z=[2,15] |
| Slide channel | 52→14 in | 7×41 cells, x=[47,53], z=[15,55] |
| Landing pool | 14 in | 57×21 cells, x=[22,78], z=[53,73] |
| Central pillar | 72 in | 9×6 cells, x=[46,54], z=[62,67] |
| West/east baffles | 20 in | x=[22,44] and [56,78], z=[68,70] |
| Center passage | 14 in | 11×3 cells, x=[45,55], z=[68,70] |
| Lip | 18 in | 57×1 cells, x=[22,78], z=74 |
| Back pool | 14 in | 57×12 cells, x=[22,78], z=[75,86] |

## Physics: Slope + Inertia

The slide floor descends 38 inches (≈ 3.2 feet) over 40 cells (40 feet of
horizontal run). The free-surface slope in the channel — once water is shallow
and moving — roughly matches the terrain slope:

```
slope ≈ 3.2 ft / 40 ft ≈ 0.079
```

The gravitational acceleration along the slide:

```
a = g · sin(θ) ≈ g · slope = 32.174 × 0.079 ≈ 2.55 ft/s²
```

Starting from rest at the top, a water parcel would theoretically reach:

```
v = √(2 · a · distance) = √(2 · 2.55 · 40) ≈ 14.3 ft/s
```

In practice, some of that is lost to the upwind diffusion of the continuity
scheme and the small residual damping. But the water does arrive at the pool
noticeably faster than it left the reservoir.

Compare this with the slosh basin (CPU 13): there, the flat floor means there
is no net gravitational acceleration along the direction of travel. The only
driving force is the initial head difference from the added-water mound. The
water decelerates as the mound flattens. On the slide, the terrain slope
provides a constant driving force the entire way down.

## Step Loop

Identical to CPU 13 — no changes to the solver. The MAC equations are:

```
1. u[x+½,z] -= dt · g · (η[x+1,z] − η[x,z]) / Δx   ← slope creates persistent u
2. u *= (1 − α·dt)                                     ← small drag
3. h updated via upwind fluxes from u, v               ← depth follows velocity
```

The terrain slope enters step 1 through `η = terrain + h`. Even when `h` is
thin and nearly uniform, `terrain` provides a persistent head gradient that
keeps accelerating the face velocities downhill.

## Default Brush

The default "Add Water" position (when no cell is selected) is `(50, 8)` —
the center of the top reservoir. Click "Add Water" without selecting anything
first and the water appears at the top of the slide.

For a more dramatic effect, click somewhere in the reservoir (z=2..15) to
select it, then add water multiple times to build up a larger head before the
flow into the channel fully develops.

## What To Watch

- **First add**: a disc of water at (50, 8) appears (24-inch-deep fill),
  drains into the channel, and the front accelerates down the slope. Use
  Step (x100) two or three times to see the surge enter the pool. Use
  Step (x1000) to jump straight to fully-developed pool waves.

- **Pool surge**: the arriving water spreads east and west from the channel
  exit (x=47..53, z=53..55) and hits the pool side walls at x=22 and x=78.
  Watch the reflected waves come back and interfere at the center.

- **Velocity display**: in the split renderer, set display mode to speed
  (debug gain ≈ 0.15). You will see the bright colour appear first in the
  channel, intensify toward the bottom of the slide, and then spread into
  the pool with the surge.

- **Multiple fills**: add water several times at the reservoir without
  stepping. When you release, a series of waves enters the channel, each
  accelerating into the previous. The pool receives a pulse-train.

- **Pillar split**: the surge from the channel (x=47..53) hits the solid
  pillar (x=46..54) almost head-on and divides into two streams that race
  around the east and west sides. Watch which side arrives at the baffles
  first and whether they constructively interfere when they meet again in
  the center passage.

- **Baffle overflow**: the east and west baffles (20in floor) sit 6in above
  the pool floor. The surge must either squeeze through the 11-cell center
  passage or build enough depth to spill over the top. Add water several
  times to build head and watch the overflow cascade.

- **Back pool fill**: once water clears the baffles and the main pool depth
  reaches ~4in, it overflows the 18in lip into the back pool. After the main
  pool quiets and drains below lip height, water is trapped in the back pool
  and cannot return.

- **Damping slider**: lower damping (0.01) means the pool waves bounce for
  a long time. Higher damping (0.5) means the pool damps out quickly while
  the slide channel still carries water — you can see where energy dissipates.

## Implementation

This experiment reuses `SimpleSloshMacSim` with two constructor arguments:

```cpp
m_sims.push_back(std::make_unique<sim::SimpleSloshMacSim>(
    "Water Slide",
    sim::SeedMapProfile::WaterSlide,
    50, 8,        // default brush at top of reservoir
    24.0f));      // deeper fill so the slide has enough head to run
```

The terrain is built by `build_water_slide_seed()` in `main.cpp`. The linear
slope is computed with a single lerp:

```cpp
// k_slide_z0=15, k_slide_z1=55 → 40-cell channel
const float t = float(z - k_slide_z0) / float(k_slide_z1 - k_slide_z0);
const int floor_h = round(k_slide_top + t * (k_slide_bot - k_slide_top));
```

No new solver code. No new class. Just a seed map and a name.

## Files

| File | Purpose |
|---|---|
| `sim/simple_slosh_mac_sim.h` | MAC solver — unchanged physics, added constructor params |
| `sim/i_field_sim.h` | `SeedMapProfile::WaterSlide` added to enum |
| `main.cpp` | `build_water_slide_seed()`, CPU 14 registration, lesson entry |
| `lesson_experiment_water_slide.md` | This lesson |

## Takeaway

The water slide demonstrates that the MAC grid's inertia is not just about
wave reflection — it is also about sustained acceleration on a slope. Every
step, the terrain gradient pushes the face velocities a little faster. Over
67 feet of slide they add up to a genuine surge that the flat-basin pipe model
cannot reproduce at all.
