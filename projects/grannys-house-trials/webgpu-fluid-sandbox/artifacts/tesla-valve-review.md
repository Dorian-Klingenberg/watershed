# Tesla valve layout review

## What changed

Both old Tesla presets used hand-made return loops, so the solver was being asked to test invented geometry rather than Tesla's layout. They are now replaced by one terrain generated from the plan view in Fig. 1 of US1329559A, reproduced in Nguyen et al. (2021). The plan is mirrored horizontally so patent port 5, which Tesla describes as the lower-resistance direction, is the app's left-to-right direction.

The map is 192 × 128 cells at the app's existing 1 ft per cell. A uniform transform keeps the patent plan's proportions: the body is 16 cells across and about 172 cells long. The floor is level, with 48 in raised banks and partitions. A two-cell axial trunk stays direct; angled patent partitions shape the hooked side passages. Printed flow arrows, labels, screw holes, and the housing outline are not terrain. At one-foot resolution some narrow openings close, so the generator reopens shortest grid throats and checks that every channel cell connects to the ports.

This is much closer to the patent drawing than the previous D/U-loop sketches because it preserves the repeated, alternating partitions and the side clearances that cause reverse flow to turn back and rejoin. It remains a coarse open-channel elevation map of a patent plan, not a dimensionally faithful closed conduit. In particular, the new body is 16 ft across at the app's 1 ft cells; keeping a 4 ft cross-section would require a smaller cell size and corresponding changes to all solver control-volume calculations.

## What the solver says so far

The current built-in controlled test starts with a wetted channel and swaps equal 18/12 in reservoir levels. With Staggered MAC and velocity advection enabled, its tick-1600 main-section result was 3.0691 ft³/s left-to-right and −2.9196 ft³/s right-to-left. The off-axis section was 3.5134 and −4.0813 ft³/s. The channel passes water both ways; the larger reverse off-axis flow is consistent with greater diversion in that transient sample.

These values do not prove diodicity. They are last-tick measurements at a fixed head, not a settled equal-flow pressure-drop ratio. The depth-averaged MAC solver does not resolve the 3D separation, wall layers, or turbulent mixing responsible for much of the real valve's direction-dependent loss. The current terrain is suitable for exploring flow paths and comparing repeatable trials, not for claiming a validated fluidic diode.

## Drawing and source

- [Top-down cell map](tesla-junction-playground/map.png)
- [Terrain JSON used by both presets](tesla-junction-playground/terrain.json)
- [Patent plan crop used for the stamp](tesla-junction-playground/patent-plan-view.png)
- [Geometry checks and throat-repair coordinates](tesla-junction-playground/geometry-checks.json)

The patent describes a favorable path that proceeds nearly straight with small deflections and an unfavorable path repeatedly redirected by the buckets/partitions. Do not interpret patent arrows as walls or as guaranteed streamlines: the solver must produce the water path. The raster tests verify a connected opening, the open direct trunk, source orientation, and that both presets load identical terrain.

Sources: Nikola Tesla, [US1329559A, “Valvular conduit”](https://patents.google.com/patent/US1329559A/en); Nguyen et al., [“Early turbulence and pulsatile flows enhance diodicity of Tesla's macrofluidic valve”](https://www.nature.com/articles/s41467-021-23009-y), Nature Communications 12, 2884 (2021), DOI 10.1038/s41467-021-23009-y. The repository's source figure is the article's Fig. 1, CC BY 4.0.
