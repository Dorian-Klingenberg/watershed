# Grass Field 003 — Build Plan

## What This Project Is

`grass-field-003` is a fresh build of the grass field erosion system.

It uses `grass-field-001` and `grass-field-002` as **curriculum**, not as starting
templates. Every piece is written from first principles so you understand exactly
why it exists before you add the next one.

**Core goal for this first arc:** a running D3D12 window with ImGui controls that
can step a `sim::GravityErosionField` and show the result in a shader-rendered
column field.

---

## External Courses (Curriculum Supplements)

Two YouTube courses are being followed alongside this project. Each is linked where
it is most relevant in the learning sequence below.

- **Chili** — ChiliTomatoNoodle "C++ 3D DirectX Programming"
  - Full playlist: https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  - Style: no-sugar-coating, uses D3D11 first then migrates to D3D12. Still worth
    watching the D3D11 episodes — they teach what D3D12 makes explicit.
  - Best for: Win32 window setup, first triangle, ImGui integration.

- **JAPG** — Just Another Programming Guide "DirectX 12 Tutorial"
  - Full playlist: https://www.youtube.com/playlist?list=PLD3tf_aBsga1A9B7UoDkM-yObxlLh9pku
  - Style: structured and textbook-like, goes straight to D3D12 without a D3D11
    detour. Linear and clean.
  - Best for: D3D12 device init, descriptor heaps, root signatures, PSOs, upload
    heaps, HLSL shader loading.

---

## The Two Reference Projects

### `grass-field-001`

What it teaches:
- How to build a D3D12 device, swap chain, descriptor heaps, command lists, and
  fence synchronization from nothing
- How to compile and load HLSL shaders at build time with `fxc`
- How `sim::GrassField` and `sim::GravityErosionField` represent terrain data
- How column-raycast rendering works in HLSL
- What coarse / refined / hybrid PSO modes look like and why they exist

What it is bad at teaching:
- It is a ~130KB single-file god-object. Everything is inside one anonymous
  namespace. Hard to read in isolation.

### `grass-field-002`

What it teaches:
- How to structure the same D3D12 loop inside a clean `Application` struct
- How to integrate ImGui (`imgui_impl_win32` + `imgui_impl_dx12`) over a D3D12
  swap chain
- How ImGui and native Win32 messages coexist
- How to keep UI panels separate from simulation state

What it re-uses:
- The same shared HLSL shader from 001 (points directly at
  `../grass-field-001/shaders/grass_field_renderer.hlsl`)
- The same shared `grannys_house_trials::core` modules

---

## The Learning Sequence

Each step below is one building block. Do not skip ahead. The whole point is to
know what every line does before you write the next.

---

### Step 1 — Bare Win32 Window

**What we build:**
- `WinMain` entry point
- Register a `WNDCLASSEX`, create a `HWND`, run a `PeekMessage` loop
- Window closes cleanly on `WM_DESTROY`

**What you learn:**
- The Windows application lifecycle: `WinMain` → `RegisterClassExW` →
  `CreateWindowExW` → message loop → `PostQuitMessage`
- Why `PeekMessage` (not `GetMessage`) is used in real-time loops
- How `WM_SIZE` matters for a D3D12 swap chain later

**Reference in 001:** first ~80 lines of `main.cpp`, `window_proc`
**Reference in 002:** `window_proc` at the top of `main.cpp`
**Watch first (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → The Win32 window setup episodes at the start of the series cover
    `WinMain`, `WNDCLASSEX`, `CreateWindowExW`, and the `PeekMessage` loop.

---

### Step 2 — D3D12 Device and Swap Chain

**What we build:**
- `IDXGIFactory`, `IDXGIAdapter`, `ID3D12Device` creation
- `ID3D12CommandQueue`, `IDXGISwapChain3` (two frame buffers)
- `ID3D12DescriptorHeap` for RTV (render target views)
- `ID3D12CommandAllocator` + `ID3D12GraphicsCommandList` per frame
- `ID3D12Fence` + `HANDLE` event for frame synchronization
- Clear the back buffer to a solid color each frame

**What you learn:**
- Why D3D12 is explicit: you manage every barrier, every descriptor, every sync
- The resource state machine: `D3D12_RESOURCE_STATE_PRESENT` ↔
  `D3D12_RESOURCE_STATE_RENDER_TARGET`
- What a descriptor heap is and why RTV descriptors live there
- The CPU/GPU fence pattern: signal on submit, wait before reusing the allocator

**Reference in 001:** device/swap-chain/fence init block, `wait_for_frame`
**Reference in 002:** same block, plus `d3d12_context.h` in `modules/gfx`
**Watch first (JAPG):** https://www.youtube.com/playlist?list=PLD3tf_aBsga1A9B7UoDkM-yObxlLh9pku
  → The D3D12 init videos cover device/adapter selection, DXGI swap chain,
    RTV descriptor heaps, command queue/list setup, and fence synchronization
    in a clean linear order — much easier to follow than the 001 monolith.
**Then cross-reference (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → His D3D12 device setup episodes confirm the same pattern from a
    second angle, and his commentary on why D3D12 is explicit is useful.

---

### Step 3 — ImGui Integration

**What we build:**
- Link `imgui.cpp`, `imgui_impl_win32.cpp`, `imgui_impl_dx12.cpp`
- Create a shader-visible `CBV_SRV_UAV` descriptor heap for ImGui's font texture
- `ImGui_ImplWin32_Init`, `ImGui_ImplDX12_Init` in app startup
- Route `WM_` messages through `ImGui_ImplWin32_WndProcHandler`
- Each frame: `NewFrame` → build UI → `Render` → `ImGui_ImplDX12_RenderDrawData`
- `ImGui_ImplDX12_Shutdown` / `ImGui_ImplWin32_Shutdown` on exit

**What you learn:**
- Why ImGui needs its own descriptor heap (it uploads font atlas to the GPU)
- Why the descriptor heap must be shader-visible (vs CPU-only RTV heap)
- The ImGui frame lifecycle and where it sits inside the D3D12 command list
- How immediate-mode UI differs from retained-mode Win32 controls

**Reference in 002:** `main.cpp` ImGui init block, `imgui_impl_dx12` setup
**Watch first (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → He has a dedicated ImGui integration episode that covers
    `ImGui_ImplWin32_Init`, `ImGui_ImplDX12_Init`, and routing `WM_` messages
    through `ImGui_ImplWin32_WndProcHandler`. Watch this before touching ImGui
    init in your own code.

---

### Step 4 — Simulation Data: GrassField and GravityErosionField

**What we build:**
- Construct a `sim::GrassField` (100×100 coarse columns)
- Construct a `sim::GravityErosionField` seeded from the `GrassField`
- Add an ImGui "Step Erosion" button that calls `erosion_field.step()`
- Print column heights in an ImGui text block to confirm state changes

**What you learn:**
- What `sim::GrassField` stores: per-column heights and terrain materials
- What `sim::GravityErosionField` does: settles inch-scale surface particles
  one gravity step at a time
- How the simulation is completely independent of rendering — pure C++ data
- Why the sim step and render step are separate concerns

**Reference headers:**
- `modules/sim/include/grannys_house_trials/sim/grass_field.h`
- `modules/sim/include/grannys_house_trials/sim/gravity_erosion_field.h`
**No external video for this step** — it is pure C++ reading. The sim types are
  independent of the graphics stack. Read the headers, then confirm your
  understanding by adding an ImGui text block that prints column heights before
  writing any GPU code.

---

### Step 5 — HLSL Column Raycast Shader

**What we build:**
- Copy the existing `grass_field_renderer.hlsl` from 001's shader folder into
  `grass-field-003/shaders/` and read through it with fresh eyes
- Add `fxc` compilation to `CMakeLists.txt` (same pattern as 001/002)
- Understand the vertex shader: what it outputs and why
- Understand the pixel shader: how the per-pixel ray steps through column data

**What you learn:**
- What a HLSL constant buffer is (`cbuffer SceneConstants`) and how it maps to
  a `D3D12_ROOT_PARAMETER` on the CPU side
- How a structured buffer (`StructuredBuffer<ColumnData>`) uploads terrain data
  to the GPU
- How column-raycast rendering works:
  - One full-screen quad
  - Each pixel reconstructs a world-space ray from the camera
  - The ray steps down a column and finds the first occupied voxel
  - That gives height, material, and normal for shading
- What `SV_Position` and `TEXCOORD` semantics mean

**Reference file:** `subprojects/grass-field-001/shaders/grass_field_renderer.hlsl`
**Watch first (JAPG):** https://www.youtube.com/playlist?list=PLD3tf_aBsga1A9B7UoDkM-yObxlLh9pku
  → The HLSL and shader-loading episodes explain `cbuffer`, `StructuredBuffer`,
    semantics like `SV_Position` and `TEXCOORD`, and how compiled `.cso` blobs
    are loaded at runtime.
**Then cross-reference (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → His constant buffer episodes add useful intuition about the CPU↔GPU data
    layout alignment rules that will bite you if you get them wrong.

---

### Step 6 — GPU Buffer Upload and Draw Call

**What we build:**
- An `ID3D12RootSignature` matching the shader's `cbuffer` + `StructuredBuffer`
- A pipeline state object (`ID3D12PipelineState`) using VS + PS blobs
- A GPU-visible upload heap buffer for column data
- Per-frame: marshal `sim::GrassField` column heights into the upload buffer
- Record the draw call: `IASetPrimitiveTopology`, `DrawInstanced` (full-screen quad)

**What you learn:**
- How a root signature describes what the shader expects from the CPU side
- The difference between an upload heap (CPU-writable, GPU-readable) and a
  default heap (GPU-only, needs a copy command)
- Why the column data is re-uploaded each frame when the sim steps
- How a full-screen quad draw works with no vertex buffer (just `SV_VertexID`)

**Reference in 001:** root signature init, PSO creation, per-frame draw block
**Watch first (JAPG):** https://www.youtube.com/playlist?list=PLD3tf_aBsga1A9B7UoDkM-yObxlLh9pku
  → The root signature and PSO episodes cover `D3D12_ROOT_PARAMETER`,
    `D3D12_DESCRIPTOR_RANGE`, `ID3D12RootSignature`, and
    `D3D12_GRAPHICS_PIPELINE_STATE_DESC` step by step. Also covers upload heap
    vs default heap and why you map/unmap an upload buffer for CPU→GPU data.
**Then cross-reference (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → His "first triangle" episodes show the draw call recording pattern
    (`IASetPrimitiveTopology`, `DrawInstanced`) which matches what the
    full-screen quad raycast uses (no vertex buffer, just `SV_VertexID`).

---

### Step 7 — ImGui Controls for Simulation

**What we build:**
- "Step Erosion" button that advances `GravityErosionField` one cycle
- "Reset Field" button that reconstructs both sim objects
- A simple stats panel: field size, erosion cycle count, selected column height
- Mouse picking: raycast from cursor to find the hovered column and show its data

**What you learn:**
- How ImGui overlays sit in the same D3D12 frame as the world render
- Why orbit-camera input and ImGui input need to be gated (check
  `ImGui::GetIO().WantCaptureMouse` before processing camera drags)
- How to go from a 2D screen position to a 3D world-space column index

**Reference in 002:** `Application` render loop, ImGui panel blocks, mouse handling
**Watch (Chili):** https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB
  → His ImGui episodes show the right pattern for mixing ImGui input with
    camera drag input — specifically checking `ImGui::GetIO().WantCaptureMouse`
    before consuming mouse events in your own handler. Getting this wrong causes
    camera drift when clicking UI panels.
**No JAPG reference for this step** — JAPG focuses on low-level D3D12; the
  ImGui control layer is Chili's strength here.

---

### Step 8 — Orbit Camera

**What we build:**
- Integrate `gfx::OrbitCamera` from the shared module
- Right-drag to orbit, scroll wheel to zoom
- "Reset Camera" button

**What you learn:**
- How view and projection matrices are constructed and passed to the shader as
  a constant buffer
- Why orbit cameras (target + distance + yaw + pitch) are the right default for
  a top-down inspection tool
- How `DirectXMath::XMMatrixLookAtRH` and `XMMatrixPerspectiveFovRH` work

**Reference header:**
- `modules/gfx/include/grannys_house_trials/gfx/orbit_camera.h`
**Reference in 001/002:** camera init, `WM_MOUSEMOVE` / scroll handling
**No direct external video for this step** — neither course covers an orbit
  camera in a terrain-inspection context. Read the shared `OrbitCamera` header
  and source, understand the yaw/pitch/distance model, then read how 001
  and 002 wire it to `WM_MOUSEMOVE` and `WM_MOUSEWHEEL`.
**Background math (optional):** Chili covers `XMMatrixLookAtRH` and
  `XMMatrixPerspectiveFovRH` in his camera series:
  https://www.youtube.com/playlist?list=PLqCJpWy5Fohe5q3A65zg_ra7zepslRxJB

---

## File Layout For This Project

```
subprojects/grass-field-003/
  PLAN.md          ← this file
  README.md        ← short what-is-this summary
  CMakeLists.txt   ← build definition
  main.cpp         ← application entry point (starts small, grows each step)
  shaders/
    grass_field_renderer.hlsl   ← our own copy to read and annotate
```

The project links against `grannys_house_trials::core` for all shared sim/gfx
modules, the same way 001 and 002 do.

---

## What Is Deliberately Not Here (Yet)

- No yard drainage scenario (`GrannysYardScenario`)
- No evidence board or playtest session types
- No coarse / refined / hybrid PSO switching
- No agent JSON packets
- No test-agent work of any kind

Those belong to later arcs. Right now the only job is: **understand the stack from
the OS window to the shader-rendered erosion field.**

---

## Post-Arc Improvements

### Step 9 — Pluggable Simulator Interface (completed after Step 8)

**What was added:**

- `sim/i_field_sim.h` — pure abstract base class `IFieldSim` with three contracts:
  - **Rendering** (`width`, `depth`, `height_at`) — used by GPU upload, scene constants, and mouse picking
  - **Lifecycle** (`reset`) — re-seeds the simulator from GrassField heights
  - **UI** (`name`, `render_ui`) — the simulator draws its own ImGui controls

- `sim/simple_erosion_sim.h` — `SimpleErosionSim : public IFieldSim`, a thin adapter around `SimpleErosionField` that adds `reset()` and `render_ui()` (cycle count + step buttons)

- `main.cpp` — `m_erosion_field` replaced with `std::unique_ptr<sim::IFieldSim> m_sim`; all seven call sites updated to use the interface

**Why `render_ui()` on the simulator:**
Each concrete simulator owns its own ImGui panel content. Application never asks
"how many cycles have run?" or "what is your threshold?". Adding a new simulator
never requires a change to Application.

**To add a new simulator:**
1. Implement `IFieldSim` in a new `sim/my_sim.h`
2. In `Application()`, replace `std::make_unique<SimpleErosionSim>()` with `std::make_unique<MySim>()`
3. Call `reset_field()` — done

**Lesson file:** `lesson_step9.md`

---

### Step 10 — Pluggable Renderer Interface (completed after Step 9)

**What was added:**

- `gfx/i_field_renderer.h` — pure abstract base class `IFieldRenderer` with three contracts:
  - **Lifecycle** (`initialize`) — builds root signature + PSO from `RendererInitContext`
  - **Render** (`record_draw`) — appends draw calls to the open command list each frame
  - **UI** (`name`, `render_ui`) — the renderer draws its own ImGui controls

- `gfx/shader_utils.h` — shared helpers: `exe_dir()`, `to_utf8()`, `load_shader_blob()`

- `gfx/raycast_renderer.h` — `RaycastRenderer : public IFieldRenderer`; moved the original `initialize_pipeline()` + `draw_field()` logic here. SRV is PIXEL-visible.

- `gfx/wireframe_renderer.h` — `WireframeRenderer : public IFieldRenderer`; wireframe fill PSO. SRV is VERTEX-visible (heights read in VS to position quad corners).

- `shaders/wireframe_renderer.hlsl` — VB-less grid mesh: 6 vertices per column (2 tris = top face), height from `column_heights_feet[cz * W + cx]`, projected by `view_projection` from SceneConstants.

- `SceneConstants` — `view_projection` (64 bytes) added at byte 112 for mesh-based renderers. Raycast shader reads only the first 112 bytes; it is unaffected.

- `main.cpp` — `m_root_signature` + `m_pso` replaced with `m_renderers` (vector) + `m_active_renderer` (int). `initialize_renderers()` builds all renderers upfront. Renderer ImGui panel has a combo picker + `render_ui()` delegation.

- `CMakeLists.txt` — added wireframe shader compilation step for `wireframe_vs.cso` and `wireframe_ps.cso`.

**Key design decisions:**

- All renderers are initialized at startup (no GPU stall on renderer switch).
- Each renderer owns its own root signature + PSO → maximum flexibility for different binding layouts.
- SRV visibility differs: raycast uses `PIXEL`, wireframe uses `VERTEX` — both are valid because each renderer has its own root signature.
- `view_projection` is appended to `SceneConstants` so the raycast shader's existing 112-byte cbuffer layout is unchanged.

**Lesson file:** `lesson_step10.md`
