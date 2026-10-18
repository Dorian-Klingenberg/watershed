# Tesla junction playground — map proposal

[Open the map and measured velocity inset](map.png) · [Downloadable terrain data](terrain.json)

This is a playable geometry proposal, now available in the application as the **Tesla junction playground · two hooked returns** terrain preset. It adapts the asymmetric split/return mechanism into a coarse, open-channel terrain map. It is a research playground, not a validated Tesla valve.

## Geometry

- Existing map format: version 1, 96 × 64 cells, one foot per cell. Channel floor is 0 inches; surrounding terrain and islands are 48 inches high.
- Two alternating stages. Two intact 159-cell islands separate the shorter paths from the curved returns. The returns include an eight-foot straight approach to each meeting point.
- Passages are nominally four feet wide, measured normal to their continuous construction paths. Raster stair steps and junction widening mean this is not an exact four-cell average over arbitrary grid cross-sections.
- Maximum cell-centre clearance diameter is 6 feet. A conservative bound allowing for positions between cell centres is 7.42 feet. This checks the largest local circular opening against the eight-foot target; it is not the entire loop footprint or an eight-foot bound on every oblique line through a junction.
- The ideal arrival tangents differ by 146.31 degrees. This is a geometric construction choice, not a universal Tesla-valve optimum. The actual map has stair-stepped walls; the image inset shows the resulting velocities rather than assuming water follows those tangents exactly.
- The outlet leaves each meeting point approximately perpendicular to the straight return. A solid strip separates the outlet from the upstream part of that return, preventing the early merger seen in the first candidates.

The three-stage candidates packed their connections too closely once these separated return throats were added. This proposal keeps two stages to preserve the junction geometry on the existing grid. The level floor removes a built-in downhill preference.

## What the trials establish

The unchanged C# CPU MAC solver was compiled into a separate harness. Momentum advection was enabled, damping was 0.02/s, and the timestep was 0.016 seconds. The channel began wetted at 15 inches. Left and right port cells were clamped to the specified reservoir depths before and after each step; the same cells and geometry were used when reversing the heads. Existing edge draining remained enabled. Interior face transfers, beyond the clamped strips, were sampled at three sections. The harness uses reflection only to set reservoir depths and read the solver's existing face transfers; it does not modify the update equations.

Each case ran for 240 simulated seconds. Discharges below are absolute mean net flow across the middle section over the last 40 seconds.

| Reservoir depths, high / low | A → B discharge (ft³/s) | B → A discharge (ft³/s) | B → A / A → B |
| --- | ---: | ---: | ---: |
| 18 / 12 inches | 3.9623 | 3.9880 | 1.0065 |
| 24 / 6 inches | 7.2868 | 7.3596 | 1.0100 |

Both routes carry water. In the first return throat, all four sampled east-face velocities at x=27, y=22…25 are negative during A → B driving, confirming the return actually travels left toward the other arriving stream. The inset plots cell-centred velocity interpolated from the staggered face velocities at 240 seconds. These are solver results, not prescribed arrows.

All four runs remained finite and left the raised terrain dry. At the end, the spread between the three discharge probes was below 0.003 ft³/s. This checks near-steady flux agreement; it is not a full reservoir/drain mass-budget audit.

**The map produces the intended local interaction, but strong fluidic rectification has not been established.** The directional difference is only about 0.6–1%. There has been no mesh-convergence study, GPU validation, or experimental calibration. These discharge ratios at equal imposed head are not pressure-loss diodicity measurements at equal flow.

Earlier three-stage candidates with shorter outlet separation gave 0.5–0.7% differences in the opposite direction. Their velocity fields exposed premature merging into the outlet. Disabling advection on one rejected candidate changed discharge substantially; that was not treated as physical validation of the valve mechanism.

## Use as a playground

The in-app preset loads this exact `terrain.json` asset. The `Pour 12 in` button places a single-cell drop at (9, 29), rather than distributing water over the surrounding raised bank. Right-click to create or increase a spring. Use the MAC option with advection enabled when comparing with these trials. The built-in Tesla experiment controls have forcing/probes at fixed coordinates for the old preset, so their automatic measurements do not apply to this custom map.

Useful small-pour locations, given as zero-based (column, row), are (9, 29) just before the first split, (18, 39) in the first curved return, and (23, 23) at the first meeting region. They reveal splitting, return travel, and local mixing respectively. Small isolated drops on a dry map will behave differently from the prewetted, continuously driven trials above.

## Reproduce

`generate_geometry.py` regenerates `terrain.json` and geometry checks with Python, NumPy and SciPy. `render.py` regenerates the PNG with Matplotlib using the saved `junction-state.json` from the final 18/12-inch A → B trial.

With .NET 10, the linked-source harness can be run from this directory:

```sh
dotnet run --project TrialHarness.csproj -c Release -- . 18 12
dotnet run --project TrialHarness.csproj -c Release -- . 24 6
```

In the cloud session, the installed Roslyn compiler was invoked directly against the .NET 10 reference assemblies because the CLI's first-run NuGet mutex failed. The compiled harness completed the trials successfully. `flow-trials.json` records the delivered terrain SHA-256, solver source commit, sampling times and measured results.

Mechanism references: [Tesla's patent US1329559A](https://patents.google.com/patent/US1329559A/en) and [Nguyen et al., Nature Communications (2021)](https://www.nature.com/articles/s41467-021-23009-y). This map is an adaptation, not a dimensionally faithful reproduction of their closed conduit.
