# Grass Field 001

This subproject is the first graphics-side harness for
`Granny's House Trials`, and it now also serves as the first real mechanic
harness for the Granny's Yard drainage round.

Canonical mission tie-in:

> prove that a small, agent-friendly yard scenario can reveal hidden
> infrastructure, record meaningful evidence, and support a real
> drainage/repair round without losing the larger world-first identity

It renders a simple `100 x 100` square of `1-foot` terrain voxels in a D3D12
viewport hosted inside a standard Windows app shell so we can prove camera,
lighting, terrain readability, voxel inspection, and a small agent-facing
yard mechanic inside one runnable.

It intentionally reuses shared code:

- `sim::GrassField` owns the terrain and garden-related voxel facts
- `sim::GravityErosionField` owns the current inch-scale gravity-settled
  surface on top of the coarse field
- `sim::AdaptiveTerrainOwnershipField` decides which `1-foot` blocks are still
  fully coarse-owned and which upper blocks must be treated as refined
  inch-scale volume
- `sim::GrannysYardScenario` owns the yard-scale drainage objective, hidden
  dependency, legal actions, and factual evidence log
- `playtest::TurnPacket` and `playtest::EvidenceBoardView` project that shared
  sim state into agent-facing and UI-facing forms
- `gfx::OrbitCamera` owns orbit camera behavior

Renderer status:

- the current implementation now renders the field through a shader-side
  column raycast driven by uploaded terrain data
- it no longer depends on emitted cube meshes for the visible terrain
- shaders are now compiled during the build into `.cso` blobs and loaded at
  startup, so the app no longer waits on a large runtime shader compile
- shader warnings are now treated as build errors for this subproject so the
  render path stays warning-clean as it evolves
- the viewport uses separate coarse, refined, and hybrid PSOs, and mode
  switching now just swaps pipeline state instead of recompiling shader code
- fine mode now uploads and traverses only sparse promoted inch patches
  instead of a full world-sized `1-inch` grid
- hybrid mode now also traverses the coarse world first and descends into
  sparse promoted inch patches, instead of marching a full `1200 x 1200`
  fine grid
- it is not yet a complete arbitrary voxel ray marcher, cube marcher, or
  fully general shader-side voxel traversal renderer
- this subproject is now the stepping stone toward that broader renderer path

The field now has:

- a standard host window with built-in controls
- an embedded D3D12 viewport rather than a raw standalone render window
- a coarse `1-foot` interaction/world-definition grid
- authored `1-inch` detail seed data for fine additions and subtractions
- a separate gravity erosion sim that settles the inch-scale surface in
  `1-inch` steps while keeping the coarse field as the authored world
- a display selector with three modes:
  `coarse 1-foot`, `refined 1-inch`, and `hybrid adaptive`
- a field-size selector for square yard sizes, currently with quick presets
  for `50 x 50`, `100 x 100`, `150 x 150`, and `200 x 200`
- shader-side column raycast rendering for the visible field
- perlin-noise-driven column heights between `1` and `5` voxels
- a flattened homestead pad
- terraced garden beds with richer garden attributes and visible retaining edges
- a small pool of water beside the garden terraces with a defined brick rim
- one guaranteed sample patch for each currently defined terrain material
- sunlight plus distance haze to give the horizon a little more depth
- click-to-inspect voxel selection with an in-window property panel
- a copyable JSON snapshot for AI-agent ingestion based on the selected voxel
- a shared Granny's Yard objective:
  get water to the north bed without flooding the cellar edge or softening the path
- target-aware scenario actions driven by the current selection
- recent scenario events and evidence surfaced in the host UI

Current controls:

- left click: inspect a voxel
- left mouse drag: pan the field
- right mouse drag: orbit the camera
- mouse wheel: zoom
- `Reset Camera`: restore the starting orbit
- `Step Erosion`: advance the inch-scale gravity settling simulation by one cycle
- `Clear Selection`: clear the current voxel selection
- `Display Grid`: choose between authored coarse, refined `1-inch` remainder, and hybrid adaptive views
- `Field Size`: change the square yard size; this resets the field and the scenario
- `Run Target Action`: execute the currently selected legal action for the selected target
- `Advance Round`: advance the shared drainage simulation one step
- `Reset Round`: restore the scenario to its initial state
- `Highlight Selected Voxel`: toggle the in-scene selection highlight
- `Copy Agent JSON`: copy the current machine-readable voxel snapshot
- `E`: step one erosion cycle from the keyboard
- `Esc`: quit

Lighting:

- one directional sunlight source
- shader-side sun shadows across the column field
- shader-side ambient occlusion from nearby terrain
- approximate shader-side indirect bounce lighting / color bleed
- brighter sky color with soft atmospheric haze

Host UI:

- standard themed Win32 controls
- D3D viewport as a child window inside the app shell
- selection details shown directly in the host window, including current
  erosion-cycle information and inch-scale height ranges
- scenario details shown directly in the host window, including the current
  objective, focused target, legal actions, compact tokenized recent events
  that suppress repeated no-op activity until the yard changes, evidence
  counts, and round success/failure state
- selection details now also show the hybrid ownership breakdown for the
  selected coarse column: how many `1-foot` blocks remain full/coarse and how
  many top blocks have been promoted to inch refinement
- agent-facing JSON shown in the host window and copyable to the clipboard,
  now including objective text, focused target, legal actions, recent
  evidence, compact tokenized recent events, and current round outcome flags

Current erosion/render boundary:

- erosion is simulated against the full `1-inch` patch surface
- the viewport can raycast the authored `1-foot` columns, the promoted
  `1-inch` remainder above coarse-full blocks, or a hybrid adaptive
  comparison mode
- the fine view now traverses the coarse `100 x 100` field first and only
  descends into sparse promoted patches, so it does not track inch columns in
  fully coarse-owned space for rendering or picking
- the hybrid view now uses that same coarse-first sparse-patch traversal while
  still showing coarse-owned lower volume and promoted inch-scale top detail
- the repository now tracks the correct hybrid ownership model for "keep full
  foot blocks coarse, refine only mixed top blocks," and the hybrid view now
  shades coarse-owned lower volume differently from promoted inch-scale top slabs
- the renderer is still operating on heightfield-like column grids, not yet on
  a fully general arbitrary voxel volume

This subproject should stay focused on visual inspection plus one small,
shared-sim-driven yard mechanic. It should not grow its own copy of scenario
logic, tester protocol, or scoring.
