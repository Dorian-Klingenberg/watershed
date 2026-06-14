# Journal Entry — 2026-05-13

## Session Summary

**Date:** May 13, 2026  
**Project:** Granny's House Trials — Grass-Field-003 Step Snapshots  
**Agent:** GitHub Copilot (Claude Sonnet 4.6)

---

## Context: Where Grass-Field-003 Stood at the Start

Grass-Field-003 is a deliberate, step-by-step DirectX 12 learning project.
The philosophy is simple: every time a meaningful concept is added, the source
file is copied and frozen as `main_stepN.cpp`. Future steps never touch the
frozen files. At the end of the project, you can open any two adjacent step
files side-by-side and see exactly what changed and why.

By the time this session began, the live `main.cpp` was at **Step 10** — the
most fully-featured build, with a pluggable simulator, a pluggable renderer,
and a GPU info/frame-timing panel. But the step discipline had slipped during
the rapid development of Steps 8, 9, and 10: the three frozen snapshots had
never been created. Only steps 1–5 and 7 were on disk. Steps 6, 8, 9, and 10
were missing entirely.

Additionally, `main.cpp` itself still bore the title "Step 8: Orbit Camera"
in both its file header comment and its `k_window_title` constant — a cosmetic
bug that had been carried silently through two subsequent steps.

---

## What We Did Today

### 1. Audited the Full Diff: main_step7 → main.cpp

Before writing a single line, both files were read thoroughly — `main_step7.cpp`
(1,400+ lines) and `main.cpp` (1,473 lines) — to map every addition, removal,
and substitution between them. This produced a precise three-level catalogue:

| Snapshot | Core Delta from Previous |
|----------|--------------------------|
| **Step 8** | Replaced fixed camera with `gfx::OrbitCamera`. Right-drag orbits, scroll zooms. `m_inv_vp` / `m_camera_eye` cached for picking. Direct `SimpleErosionField` member. Direct root sig / PSO pipeline. |
| **Step 9** | Replaced `SimpleErosionField` with `std::unique_ptr<sim::IFieldSim>`. All sim call sites go through the interface. `SimpleErosionSim` is the first concrete adapter. Pipeline unchanged. |
| **Step 10** | Added pluggable renderers (`IFieldRenderer`, `RaycastRenderer`, `WireframeRenderer`). `SceneConstants` gained `view_projection` (176 bytes total). GPU adapter name + VRAM query. QPC frame-time EMA (α=0.05). `initialize_pipeline()` replaced by `initialize_renderers()`. |

### 2. Created `main_step8.cpp` — Orbit Camera + Direct Pipeline

Step 8 was reconstructed by starting from `main_step7.cpp` and surgically
applying only the Step 8 changes:

- Added `gfx/orbit_camera.h` and its namespace alias
- Replaced the fixed-camera members with `gfx::OrbitCamera m_camera{-90.f, 45.f, 130.f}`
- Added `m_right_dragging`, `m_drag_last`, `m_inv_vp`, `m_camera_eye` members
- Converted `update_scene_constants()` to drive from spherical orbit coordinates
  and cache `m_inv_vp` / `m_camera_eye`
- Converted `update_mouse_picking()` to use the cached matrices instead of
  recomputing them (Step 7 recomputed them inline)
- Added `handle_camera_input()` — WantCaptureMouse gate, right-drag orbit, scroll zoom
- `reset_field()` directly assigns `m_erosion_field = SimpleErosionField(...)`
- `draw_field()` issues D3D12 commands directly: `SetRootSignature`,
  `SetDescriptorHeaps`, `SetGraphicsRootCBV`, `SetGraphicsRootDescriptorTable`,
  `SetPipelineState`, `DrawInstanced(3,1,0,0)`
- `render_imgui()`: no GPU stats, no renderer picker; calls `handle_camera_input()`
  first, then Renderer / Camera / Simulation / Selected Column panels
- Title: `L"Grass Field 003 — Step 8"`

The file is fully self-contained at ~68 KB and compiles cleanly with no
references to `IFieldSim`, `IFieldRenderer`, or any Step 9/10 additions.

### 3. Created `main_step9.cpp` — Pluggable Simulator Interface

Step 9 was built by starting from `main_step8.cpp` and adding the simulator
abstraction layer only. Key changes:

- Added `#include <memory>`, `"sim/i_field_sim.h"`, `"sim/simple_erosion_sim.h"`
- Replaced `sim::SimpleErosionField m_erosion_field{}` with
  `std::unique_ptr<sim::IFieldSim> m_sim`
- Constructor: `m_sim = std::make_unique<sim::SimpleErosionSim>(); reset_field();`
- `reset_field()` now calls `m_sim->reset(w, d, std::move(heights))` — the
  seeding logic stays in Application so swapping the concrete type is a
  one-liner in the constructor
- All sim call sites updated: `m_sim->width()`, `m_sim->depth()`,
  `m_sim->height_at()`, `m_sim->name()`, `m_sim->render_ui()`
- The Simulation ImGui panel gained `m_sim->name()` display and delegates
  all simulator-specific controls to `m_sim->render_ui()`
- The renderer pipeline (`m_root_signature`, `m_pso`, `initialize_pipeline()`,
  inline `draw_field()`) is **identical to Step 8** — no renderer changes

Step 9 is the clearest possible statement of the single-responsibility
principle: the application orchestrates but never knows what simulator
is running. To swap simulators: change the `make_unique<>` call. Nothing else.

### 4. Created `main_step10.cpp` and Fixed `main.cpp`

Step 10 required no reconstruction — it is the current `main.cpp`. The only
work needed was fixing the stale header. Two changes were made to `main.cpp`:

```
Line 1:  // grass-field-003 — Step 8: Orbit Camera
      →  // grass-field-003 — Step 10: Pluggable Renderer + GPU Panel

Line 69: constexpr wchar_t k_window_title[] = L"Grass Field 003 — Step 8";
      →  constexpr wchar_t k_window_title[] = L"Grass Field 003 — Step 10";
```

`main_step10.cpp` was then created by copying the corrected `main.cpp`
verbatim. The interior comments that reference "Step 8" (e.g.
`// ── Step 8: orbit camera ──`) were intentionally left as-is: they describe
when a feature was *introduced*, not what step the file represents.

### 5. Wired All Step Snapshots into CMakeLists.txt

Before today, only `main.cpp` had a build target. The nine snapshot files
existed as source artifacts but produced no executables. This was fixed by
adding a CMake helper function `add_gf003_step()` that:

- Creates a `WIN32` executable per step (`grannys_house_trials_grass_field_003_stepN`)
- Assigns the correct source groups per step:
  - Steps 1–2: Win32 / D3D12 only (no ImGui, no sim TU)
  - Step 3: + ImGui six TUs
  - Steps 4–10: + ImGui + `sim/simple_erosion_field.cpp`
- Links the same libraries as the main target (`d3d12`, `dxgi`, `d3dcompiler`,
  `grannys_house_trials::core`, etc.)
- Adds a dependency on the shader compilation custom target so every step exe
  has access to a fresh `shaders/` folder
- Sets `VS_DEBUGGER_WORKING_DIRECTORY` to the repo root (matching the convention
  from the step-progression instruction file)
- Groups all step targets under
  `"Granny's House Trials/Subprojects/Step Snapshots"` in the IDE solution tree

### 6. Full Build — Zero Errors

CMake re-configured automatically (the `CMakeLists.txt` timestamp triggered it),
then MSBuild compiled all targets:

```
grannys_house_trials_grass_field_003_step1.exe  ✅
grannys_house_trials_grass_field_003_step2.exe  ✅
grannys_house_trials_grass_field_003_step3.exe  ✅
grannys_house_trials_grass_field_003_step4.exe  ✅
grannys_house_trials_grass_field_003_step5.exe  ✅
grannys_house_trials_grass_field_003_step7.exe  ✅
grannys_house_trials_grass_field_003_step8.exe  ✅
grannys_house_trials_grass_field_003_step9.exe  ✅
grannys_house_trials_grass_field_003_step10.exe ✅
grannys_house_trials_grass_field_003.exe        ✅  (main / live Step 10)
```

All targets compiled with `/W4` and no warnings. The shader custom target
also ran cleanly, producing `grass_field_vs.cso`, `grass_field_ps.cso`,
`wireframe_vs.cso`, and `wireframe_ps.cso`.

---

## Why This Matters

The step snapshot system is not just bookkeeping — it is the teaching artifact.
Grass-Field-003 is a learning project, and the whole point is that you can sit
down with two adjacent step files and understand every line that changed and
exactly why. With steps 8, 9, and 10 missing, the most interesting part of the
project — the transition from hard-coded infrastructure to pluggable interfaces
— had no record.

Now the full arc is on disk and buildable:

| Steps | Concept Illustrated |
|-------|---------------------|
| 1 → 2 | Blank Win32 window → D3D12 device + swap chain |
| 2 → 3 | Raw D3D12 → ImGui overlay |
| 3 → 4 | Static display → live simulation data |
| 4 → 5 | CPU rendering → HLSL raycast shader |
| 5 → 7 | Flat scene → orbit mouse picking |
| 7 → 8 | Fixed camera → interactive orbit camera |
| 8 → 9 | Concrete sim → `IFieldSim` interface (pluggable simulator) |
| 9 → 10 | Hard-coded pipeline → `IFieldRenderer` (pluggable renderer + GPU stats) |

Anyone who wants to understand how the project reached its current shape can
walk the steps in order, run each one, and read the diff. That is the intended
value of this project.

---

## Technical Notes for Future Reference

**`SceneConstants` size progression:**
- Steps 5–9: 112 bytes (`inverse_view_projection` 64 + `camera_world_pos` 16 + field params 32)
- Step 10: 176 bytes (+ `view_projection` 64 for the wireframe VS)
- Upload buffer is always `k_cb_aligned_size` = 256 bytes regardless

**Why `main_step6.cpp` is missing:**
Step 6 was never committed and no source was found. It is presumed to have been
an intermediate state between step 5 and step 7 that was skipped or merged.
Its absence is noted in the step-progression instruction file. It does not
affect the arc of the project because the conceptual jump from 5 to 7 is clear.

**`handle_camera_input()` runs inside `render_imgui()`:**
This is intentional. It must run after `ImGui::NewFrame()` so ImGui's IO is
populated with fresh mouse state. The one-frame lag this introduces (camera
changes take effect next frame) is ~16ms at 60fps — completely imperceptible.

**The `make_unique<>` / `reset_field()` pattern (Step 9):**
`reset_field()` owns the seeding logic (reads from `GrassField`, builds the
height vector, calls `m_sim->reset()`). It does not construct the simulator —
that is the constructor's job. This split means swapping the active simulator
at runtime requires only replacing `m_sim` and calling `reset_field()`.
`Application` never needs to be changed for a new simulator type.

---

## What Comes Next

The snapshot archive is complete. The immediate logical next steps are:

1. **Add `.vscode/launch.json` entries for each step snapshot** — the IDE Run &
   Debug dropdown does not auto-populate from CMake; each exe needs a manual
   entry. The instruction file for this pattern already exists in
   `.github/instructions/`.

2. **Step 11 planning** — candidates include: depth buffer + proper 3D
   geometry, shadow pass, or a second `IFieldSim` implementation (e.g., a
   hydraulic erosion simulator) to exercise the pluggable interface for real.

3. **Step lesson files** — `lesson_step8.md` and `lesson_step9.md` were
   written in a prior session. `lesson_step10.md` is on disk. These should be
   reviewed to confirm they accurately reflect the frozen snapshots created today.

---

*Journal authored by GitHub Copilot (Claude Sonnet 4.6) at end of session.*
