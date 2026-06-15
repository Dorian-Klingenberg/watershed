# Handoff — Grass Field 003 Slosh Session
## 2026-06-14

---

## What Was Built

### 1. Camera and UI polish ported from grass-field-004 (Step 0.5)

All of the improved input handling from 004 was backported to 003:

- Middle-drag orbits the camera around the selected cell (or field center).
- Right-drag pans by anchoring to the world plane at the selected cell's
  surface height. The pan accumulates into `m_focus_offset_x / _z` so Reset
  Camera brings it back to origin.
- Right-click without drag selects the hovered column.
- Scroll wheel zooms.
- Window resize handled correctly via `handle_pending_resize()` in the render
  loop with a deferred `WM_SIZE` flag — no resize during GPU frame.
- DDA + AABB picking replaces the old flat-Y projection.
- Hover highlight (`highlight_x / highlight_z` in `SceneConstants`) applied in
  all three HLSL renderers (grass_field, split_lod, wireframe) via lerp to
  a yellow tint.

### 2. CPU 12 — Slosh Basin Flow (`SimpleSloshPipeFlowSim`)

Located: `sim/simple_slosh_pipe_flow_sim.h` (wrapped by
`sim/simple_slosh_basin_flow_sim.h`)

Key characteristics:
- 4 cardinal pipes per cell (no diagonals): E(0) W(1) S(2) N(3), opposite
  of d = d ^ 1.
- Gravity impulse: `flux += dt * pipe_area * g * (η_src − η_nbr) / cell_length`
- Linear drag: `flux *= (1 - damping * dt)` — default damping = 0.06/s
- Gate: if flow tries to cross a terrain wall, zero the pipe flow rate
- Two-pass positivity-preserving volume transfer (same pattern as terrain-head)
- Sleep: 120 quiet steps at max_flow < 0.002 ft³/s → zero pipes, stop
- Pipe area = 0.20 ft², dt = 0.016 s

**Note:** The basin terrain was fully rebuilt in this session. The original
Gaussian terrain (bowl, shelf, island, baffle, rim, spillway via `std::exp()`)
was replaced with hard step-function geometry. Every feature now has a vertical
face. Details in `build_slosh_basin_seed()` in `main.cpp`.

Basin map summary (100×100 ft, 1 cell = 1 ft):
- Floor: 16 in
- Rim: 72 in (4-cell border)
- Spillway: 50 in (south rim x=74..88)
- Central pillar: 72 in (x=43..58, z=43..56)
- East wall: 72 in (x=67..69, z=4..72, open gap z=73..96)
- Diagonal baffle: 72 in (rasterized line from [22,76] to [60,28], half-width 1.8)
- West ledge: 34 in (x=4..20, z=34..66)

### 3. CPU 13 — Slosh MAC Grid (`SimpleSloshMacSim`)

Located: `sim/simple_slosh_mac_sim.h`

Registered as the 13th sim (index 12 in `m_sims`). Returns
`SeedMapProfile::SloshBasin`, so it uses the same hard-step terrain as CPU 12.

Algorithm: linearised depth-averaged shallow water equations on a staggered
MAC grid (Harlow & Welch 1965, doi:10.1063/1.1761178).

Face layout:
- `u_[ z*(W+1) + x ]` — east-face velocity, x ∈ [0, W]
- `v_[ z*W + x ]` — north-face velocity, z ∈ [0, D]

Step loop per frame:
1. `apply_gravity()` — `u -= dt*g*(η_right−η_left)/Δx` for active faces
2. `apply_drag()` — `u *= (1 − damping*dt)`, same for v
3. `update_depth()` — upwind fluxes from u/v into h, swap h/h_new
4. `tick_sleep()` — monitor `last_max_speed_`, sleep after 120 quiet steps

Solid handling: `u_active_` and `v_active_` boolean masks precomputed at
reset(). A face is active only if both adjacent cells have terrain < 60 in.
Inactive faces are never written. Hot loop is branch-free on the common path.

Constants:
- `k_dt = 0.016 f`
- `k_gravity = 32.174 f`
- `k_solid_in = 60` (threshold in inches)
- `k_add_radius = 4`, `k_add_depth_in = 8.0 f`
- `damping_ = 0.02 f` default (nearly frictionless)
- `k_sleep_steps = 120`, `k_sleep_speed = 0.005 f ft/s`

### 4. Documentation

- `lesson_experiment_slosh_basin_flow_sim.md` — rewritten to accurately
  describe CPU 12 as it now exists (hard-step terrain, linear drag pipe model)
- `lesson_experiment_slosh_mac_grid_sim.md` — new, covers the MAC grid layout,
  the three-step algorithm, wave speed derivation, stability, solid walls,
  limitations, and the Harlow & Welch paper
- `LESSON_CATALOG.md` — CPU 13 added; ordering table updated to 18 entries;
  mermaid flow diagram extended

---

## Current State Of All Changed Files

| File | Status |
|---|---|
| `main.cpp` | +include slosh_mac, +sim registration, +lesson entry (array size 17→18), +CPU13 lesson pointing at new .md, rebuilt slosh basin terrain |
| `sim/simple_slosh_mac_sim.h` | New |
| `sim/simple_slosh_pipe_flow_sim.h` | Updated constants: pipe_area 0.10→0.20, damping 0.5→0.06, add_radius 6→4, add_depth 22→8, sleep threshold, slider range |
| `sim/simple_slosh_basin_flow_sim.h` | Description text only |
| `shaders/grass_field_renderer.hlsl` | highlight_x/z in cbuffer |
| `shaders/split_lod_renderer.hlsl` | highlight_x/z, world_xz texcoord, highlight tint |
| `shaders/wireframe_renderer.hlsl` | highlight_x/z, nointerpolation cell_x/z, highlight return |
| `lesson_experiment_slosh_basin_flow_sim.md` | Rewritten |
| `lesson_experiment_slosh_mac_grid_sim.md` | New |
| `LESSON_CATALOG.md` | CPU 13 added, table updated |

Build: `cmake --build --preset build-vs2026-debug` — clean, no errors.

---

## Known Issues And Tuning Notes

**Wave speed visibility.** At k_dt = 0.016 s and `c = √(g·H)`, a wave in
8-inch-deep water moves at ~4.6 ft/s. If `step_once()` is called once per
frame at 60fps, the wave advances ~0.077 cells per frame — very slow. The
"Step (x100)" button is the intended way to fast-forward. If this feels wrong
after testing, the options are:
  a) Increase k_dt (e.g. 0.05 s) — still stable, 3× faster apparent speed
  b) Add sub-stepping inside `step_once()`
  c) Call `step_once()` N times per frame in the render loop

**CPU 12 pipe model ceiling.** The pipe model is useful for comparison but
has a fundamental stability-vs-speed coupling through `k_pipe_area`. These
constants were tuned for stability at thin water depths (below ~1.5 in).
They are not wrong but represent a deliberate tradeoff rather than optimal
slosh performance.

**No advection in MAC.** The `u·∂u/∂x + v·∂u/∂z` terms are omitted from
the momentum equations. This means large-amplitude waves don't steepen and
hydraulic jumps don't form. Adding semi-Lagrangian advection is the natural
next step for the MAC sim.

**Solid threshold.** `k_solid_in = 60` means terrain >= 60 inches is treated
as rigid wall. The west ledge (34 in) is fluid. The spillway (50 in) is also
fluid — flow can pass through it at water levels above 50 in. If any future
feature needs terrain at 61-71 in that should be passable, the threshold needs
to move or become per-cell.

---

## Suggested Next Steps

1. **Test and tune wave visibility.** Run the MAC sim, add water near one wall,
   use Step (x100), watch whether the wave front crosses the basin clearly.
   Adjust k_dt or sub-stepping if the apparent speed is too slow.

2. **Add advection to MAC.** The `u·∇u` term. Requires interpolating the
   cross-component velocities to the face being updated. A semi-Lagrangian
   trace-back scheme (look up where this parcel of velocity came from one step
   ago) is the cleanest approach.

3. **GPU version.** The MAC step loop is embarrassingly parallel. The gravity
   and drag steps update each face independently. The continuity step reads
   four face neighbors and writes one cell — a straightforward compute shader.

4. **Jeremy and Richard playtest personas.** Still incomplete from before
   this session. See project memory: `project_jeremy_richard_work_needed.md`.
