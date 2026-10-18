# Tesla valve map

The app now has one selectable Tesla terrain: the blue route stamped into the
user-approved 192 × 128 grid. The top-down PNG is
[`wwwroot/tesla-valve-layout.png`](../wwwroot/tesla-valve-layout.png); its
cell-by-cell elevations are in [`tesla-valve-map.json`](tesla-valve-map.json).

Each cell is 1 ft. Blue cells are channel floor at 0 in; every other cell is
raised terrain at 48 in. The purple flow arrows and black mounting marks in the
drawing are annotations, not open water cells or terrain features. The stamp
follows the indicated channel route without adding branches or smoothing the
user's geometry.

The preferred app starting point is the left port at (8, 64). The terrain can
also be explored with right-click springs and the existing forward/reverse
reservoir controls. No new trials were run for this map. Earlier trial numbers
refer to different terrain and should not be used to characterize this layout.

The geometry follows the asymmetric return paths associated with Tesla's
valvular conduit, where reverse flow is repeatedly redirected while the
preferred direction has a more direct route. The current depth-averaged solver
is an exploration tool: it does not resolve 3D boundary layers, separated jets,
or turbulent mixing, so the raster cannot establish real-valve diodicity.

Sources: Nikola Tesla, [US1329559A, “Valvular Conduit”](https://patents.google.com/patent/US1329559A/en); Nguyen et al., [“Early turbulence and pulsatile flows enhance diodicity of Tesla's macrofluidic valve”](https://www.nature.com/articles/s41467-021-23009-y), *Nature Communications* 12, 2884 (2021), DOI 10.1038/s41467-021-23009-y.
