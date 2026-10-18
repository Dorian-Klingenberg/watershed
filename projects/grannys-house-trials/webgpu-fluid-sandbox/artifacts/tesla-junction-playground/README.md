# Patent Tesla valve terrain

[Open the top-down terrain raster](map.png) · [Terrain JSON](terrain.json) · [Patent plan crop](patent-plan-view.png) · [Raster checks](geometry-checks.json)

Both Tesla presets in the app use this same map. The first retains the controlled forward/reverse head experiment; the second is the placement-and-spring playground.

## Stamp method

The source is the plan-view panel of US1329559A Fig. 1, reproduced as panel (a) of Nguyen et al. (2021). The source plan is horizontally mirrored so patent port 5 is the left inlet and its preferred direction is left-to-right. The patent's housing outline, fastening holes, labels, dotted arrows, and centerline annotations are not terrain. Dark cells in `map.png` are the level 0 in floor; light cells are 48 in raised banks or traced partitions.

The map is 192 × 128 at 1 ft per cell. One uniform image-to-grid transform puts the patent's 10.8:1 planform into a 16-cell-wide body that is about 172 cells long. The two-cell center trunk remains straight through the device. Actual slanted partitions remain in the side passages; a single horizontal clear strip would erase most of those baffles at coarse resolution.

Rasterizing thin scan lines onto one-foot cells closed some small throats and left trapped cells. The generator reconnects each such pocket to the direct trunk through the minimum-cost cell path and records every repaired cell in `geometry-checks.json`. This avoids disconnected fluid volumes while keeping the traced partition layout. The result is a grid-scale open-channel analogue of the patent plan, not a manufactured-scale replica of the original enclosed conduit.

## Current controlled trial

The staggered-MAC model with velocity advection enabled starts with the channel wetted at 15 in and clamps clear port cells to 18/12 in, then swaps those levels for the reverse trial. At tick 1600, the main section at x=160, rows 63–64 measured 3.0691 ft³/s left-to-right and −2.9196 ft³/s right-to-left. The off-axis section at x=160 measured 3.5134 and −4.0813 ft³/s. Both directions pass water. The reverse run's larger off-axis movement is qualitatively consistent with more diversion, but these last-tick transient values do not establish steady resistance or a diodicity ratio.

The simulation remains a depth-averaged MAC model. It does not resolve the 3D boundary layers, separation and turbulent mixing central to a real Tesla valve. Use the map to explore flow and repeat controlled trials; treat measured directional bias as a model result, not physical validation.

## Reproduce

The geometry and images are regenerated with Python, NumPy, SciPy and Pillow:

```sh
python3 artifacts/tesla-junction-playground/generate_geometry.py
```

The patent source raster is stored at `../tesla-source-figure.png`; the crop is generated as `patent-plan-view.png`. Sources: Tesla [US1329559A](https://patents.google.com/patent/US1329559A/en), and Nguyen et al. [Nature Communications (2021)](https://www.nature.com/articles/s41467-021-23009-y), CC BY 4.0.
