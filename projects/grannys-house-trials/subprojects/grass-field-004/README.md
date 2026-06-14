# Grass Field 004

Visual prototype for the authored Granny's House map.

This subproject starts from the Grass Field 003 Step 10 renderer architecture:

- pluggable renderers
- one-inch render/simulation cells
- split coarse/fine column renderer
- ImGui inspection panels

The visual map uses a `108 x 156` foot authored footprint, expanded to
`1296 x 1872` one-inch cells. Broad natural ground is quantized to full-foot
column heights so it stays cheap in the split renderer. Structures and
interactive map features use inch-level detail:

- cistern
- stone canal
- terraced garden
- splitter box
- Granny's house and roof
- loose boulder wall
- safe swale and check dams
- visible/debug-labeled hidden drain routes
- lower garden basin

This pass is visual-only. It does not wire scenario actions or scoring into the
map yet.

Current interaction model:

- left-click: select/focus column
- middle-drag: orbit camera
- right-drag: pan camera focus
- scroll: zoom

Picking and camera notes:

- hover/selection uses CPU ray traversal through columns (DDA + per-cell AABB)
  instead of simple ground-plane projection, improving angled-view accuracy.
- resizing is supported through deferred swapchain/backbuffer/depth recreation
  so UI/picking and viewport dimensions remain in sync.

Lesson note:

- [lesson_2026-05-23_input_resize_picking.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-004/lesson_2026-05-23_input_resize_picking.md)
