# Tesla valve map

The app has one selectable Tesla terrain, based on the reviewed black-and-white
96 × 64 bitmap and uniformly doubled to the 192 × 128 application grid. The
bitmap is
[`wwwroot/artifacts/tesla-blue-box-stamp-96x64.png`](../wwwroot/artifacts/tesla-blue-box-stamp-96x64.png).
The top-down terrain PNG is
[`wwwroot/tesla-valve-layout.png`](../wwwroot/tesla-valve-layout.png); its
cell-by-cell elevations are in [`tesla-valve-map.json`](tesla-valve-map.json).

Each app cell is 1 ft. Short raster gaps at the joins between passage sections
and return-branch ends are closed to keep the stamped water path connected;
gaps between neighboring legs remain open. The bed falls from 24 in at the
left port to 0 in at the right port. A discrete-Laplace solve across the full
channel graph gives each merge one shared height and distributes the fall over
the routes according to their lengths. Adjacent channel cells vary by no more
than 1 in. Cells outside the channel remain raised to 48 in. The grayscale PNG
shows the grade: darker channel cells are lower.

The preferred app starting point is the left port at (8, 64). The terrain can
also be explored with right-click springs and the existing forward/reverse
reservoir controls. Controlled experiments now use equal 6 in surface-head
difference in either direction; initial channel water is set relative to a
level 36 in free surface so the sloping bed does not introduce artificial
starting puddles. Earlier trial numbers refer to different terrain and should
not be used to characterize this layout.

The geometry follows the asymmetric return paths associated with Tesla's
valvular conduit, where reverse flow is repeatedly redirected while the
preferred direction has a more direct route. The current depth-averaged solver
is an exploration tool: it does not resolve 3D boundary layers, separated jets,
or turbulent mixing, so the raster cannot establish real-valve diodicity.

Sources: Nikola Tesla, [US1329559A, “Valvular Conduit”](https://patents.google.com/patent/US1329559A/en); Nguyen et al., [“Early turbulence and pulsatile flows enhance diodicity of Tesla's macrofluidic valve”](https://www.nature.com/articles/s41467-021-23009-y), *Nature Communications* 12, 2884 (2021), DOI 10.1038/s41467-021-23009-y.
