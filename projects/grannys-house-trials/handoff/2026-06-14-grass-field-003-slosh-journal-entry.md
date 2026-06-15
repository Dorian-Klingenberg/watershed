# Journal Entry — 2026-06-14

## The Slosh Session

Today was about inertia. The question for the whole session was: why does the
water always run out of speed before it hits anything, and what is the smallest
change needed to fix that?

It turns out the answer is not a constant. It is a data structure.

---

## Porting the UI

The session started by bringing the camera and picking improvements from
grass-field-004 over to grass-field-003. Middle-drag orbits, right-drag pans on
the world plane anchored to the selected cell's surface, DDA+AABB picking instead
of flat Y=0 projection, hover highlight across all three HLSL renderers, and
proper window resize handling.

None of that was technically hard — it was a mechanical port — but it made the
rest of the session much more pleasant. Being able to grab any point on the field
and pivot the camera there is the difference between a demo you can inspect and
one you can only watch.

---

## CPU 12: Building a Map That Can Show You Anything

Before writing any physics, we rebuilt the terrain.

The original slosh basin used Gaussian blobs for every feature: an asymmetric
bowl, a shelf, a central island, a diagonal baffle, a raised rim, a spillway
notch. All of them were `std::exp(−r²)` curves. The result was a landscape of
smooth hills where water slowly climbed slopes and nothing ever hit anything
at speed.

The replacement is entirely step functions. The basin floor is flat at 16 inches.
The rim is 72 inches, vertical on all sides. The central pillar is a hard
rectangular block. The diagonal baffle is rasterized cell-by-cell — a city block
of wall, not a hill. Every feature you can see is something water will actually
collide with rather than flow over.

The physical analogy is the difference between a sandbox and a swimming pool.
In the sandbox, any wave you make immediately starts climbing the surrounding
sand. In the pool, it travels the flat bottom and hits the tile wall at full
speed.

The pipe sim (CPU 12) got its own solver at the same time: `SimpleSloshPipeFlowSim`,
four cardinal neighbors with linear drag instead of Manning friction. Manning
friction scales as `|h|^(4/3)` — it destroys momentum most aggressively at the
very moment when you have lots of it. Linear drag scales uniformly. A wave that
was moving fast yesterday is still moving almost as fast today.

---

## Hitting the Ceiling of the Pipe Model

This is where it got interesting.

Even with the better terrain and the better drag, the user described two problems:
the water either vibrated (numerical instability at thin depths) or it was "too
thick" — enough water to be stable, but so deep that the wave was invisible
against the background.

Investigating this revealed that both problems are the same problem. The stability
condition for the pipe model ties pipe area to minimum stable depth: `k_pipe_area
× g × dt / depth < 1`. A large pipe area lets waves propagate fast but requires
thick water to stay stable. A small pipe area is stable with thin water but slows
wave propagation to a crawl. There is no pipe area in the middle that gives both.

This is not something you can tune your way out of. The pipe model re-derives
flow from pressure every step. A wave "front" has no independent existence — it
is just a sequence of pressure-gradient events. When you add wave energy at one
place and it has to travel fifty feet, it has to survive fifty separate re-derivation
steps. Each step throws away the accumulated inertia and starts from whatever the
local head gradient is saying right now.

We squeezed out what we could — the best constants for the pipe model, the hard
terrain, lower default damping. But the ceiling was clear.

---

## CPU 13: The MAC Grid

The MAC grid, introduced by Harlow and Welch in 1965, stores velocity on cell
faces rather than in pipes between pairs of cells. The east-face velocity `u[x+½]`
lives between cell `x` and cell `x+1`. The north-face velocity `v[z+½]` lives
between cell `z` and cell `z+1`. Water depth lives at cell centers.

Each step, the gravity term increments the face velocities:

```
u[x+½] -= dt · g · (η[x+1] − η[x]) / Δx
```

That's an accumulation, not a replacement. A face that was already moving east
gets a little more eastward push if the right cell is higher, a little less if
the cells have equalized, and a westward push if the left cell is now higher.
The face velocity is the sum of every pressure event since the simulation started,
minus what drag has taken away.

A wave is literally a band of positive `u` values moving east across the face
array. It has its own momentum. It does not need the local head gradient to stay
alive.

The stability condition for the MAC shallow water equations is the classical CFL
criterion: `√(g·H) · dt / Δx < 1`. For the water depths in this sim, that is
comfortably satisfied at `dt = 0.016 s`. Thin water is stable. The vibrating-
or-too-thick problem is gone.

Wave speed is `c = √(g·H)`. It is a physical consequence, not a constant to tune.
Deeper water produces faster waves. Shallower water produces slower ones. You can
watch this happen by adding water near a wall and watching the wave ring expand at
different speeds in the deep basin versus the shallow west ledge.

Implementing this was satisfying because it was small. The solver is ~200 lines.
The data is four arrays: `h_`, `u_`, `v_`, `h_new_`. The only genuinely tricky
part was the precomputed `u_active_` and `v_active_` masks that keep the hot loop
from checking solid cell status every step. Everything else was a direct translation
of the equations.

---

## What the Session Felt Like

The MAC sim is really fun to play with. Add a small disc of water near the west
wall, step forward a hundred frames, and watch a wave ring expand, hit the central
pillar, split into two arcs that continue past the pillar, reflect off the east
wall, come back, and interfere with each other in the gap south of the east
partial wall. All of that physics is real — it is the actual behavior of the
shallow water equations on that geometry, not an approximation or a hack.

The hard-step terrain deserves a lot of credit here. The wave hits a cliff and
comes back. It does not slowly bleed energy climbing a ramp. The collision is
immediate and the reflection is clean.

The comparison between CPU 12 and CPU 13 is also useful for teaching. Same terrain,
same add-water button, same field. In CPU 12 you can watch the velocity drain out
of the wave as it travels. In CPU 13 the wave arrives at the far wall at the same
speed it left. The difference is one data structure and thirty years of numerical
methods becoming obvious.

---

## What Is Left

The MAC sim omits the nonlinear advection term `u·∂u/∂x + v·∂u/∂z`. This term
is responsible for wave steepening and hydraulic jumps — the behavior where a fast
wave catches up to a slow one and the combined front becomes a shock. Without it,
large-amplitude waves are always gentler than real water.

Adding advection is the next natural step. It requires interpolating the cross-
component velocity to the face being updated and running a stable advection scheme.
Semi-Lagrangian back-tracing is the classic approach.

After that, the MAC solver is parallel-friendly. The gravity and drag steps are
embarrassingly parallel. The continuity step reads four neighbors and writes one
cell. A compute shader version would be interesting to compare against the terrain
erosion HLSL experiments already in the ladder.

---

## Files Changed

- `main.cpp` — camera input, picking, resize, slosh basin terrain (hard step
  functions), CPU 13 lesson registration, array size fix
- `sim/simple_slosh_pipe_flow_sim.h` — replaced Manning with linear drag, tuned
  constants for stability at thin depths
- `sim/simple_slosh_basin_flow_sim.h` — description text
- `sim/simple_slosh_mac_sim.h` — new, full MAC shallow water solver
- `shaders/grass_field_renderer.hlsl` — highlight_x/z
- `shaders/split_lod_renderer.hlsl` — world_xz, highlight tint on water and terrain
- `shaders/wireframe_renderer.hlsl` — nointerpolation cell coords, highlight return
- `lesson_experiment_slosh_basin_flow_sim.md` — rewritten for actual current state
- `lesson_experiment_slosh_mac_grid_sim.md` — new
- `LESSON_CATALOG.md` — CPU 13 added, ordering table updated to 18 entries
