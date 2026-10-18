# Tesla valve layout review

## Findings from this conversation and the current application

The repeated mistake was to treat visible looping or a drawn flow arrow as evidence of a fluidic diode. A Tesla valve requires different pressure losses when the driving direction is reversed through the same fixed geometry.

1. **The existing six-stage preset loses its separating geometry when rasterized.** The source uses a four-cell straight trunk and return polylines with radius 1.3 cells. Reconstructing its exact 96-by-64 terrain mask and labeling solid cells with four-neighbor connectivity gives component sizes `[5395, 1, 1, 1]`. The largest component is the surrounding solid; there are only three isolated one-cell islands. Six drawn return paths therefore do not produce six resolved patent-like stages. Nearby portions of the thickened paths merge.
2. **The user's carved map demonstrated curved flow, storage and release, but previous reports overstated what that proved.** A drop interacting with a bend or pocket can show interesting inertia without proving lower resistance in one direction. The earlier comparisons were not matched forward/reverse pressure-loss measurements.
3. **The first narrowed map discarded the side-return geometry.** It recovered one main centerline and added a sine displacement, producing a meandering trench. It also defined a four-foot overall cut with only a one-foot flat bed and bank transitions. That is not four feet of uniform clear floor.
4. **The blue-route redraw replaced the intended junction geometry with smooth parallel loops.** Assigning lower grades to longer alternatives between shared-height junctions is geometrically consistent, but does not create the valve effect. Smooth splitting and merging can remove the adverse re-entry and direction-dependent deflection that matter. Prescribed arrows were not simulated streamlines.
5. **The elevation treatment confounded the test.** A downward slope naturally favors downhill transport. A Tesla valve can be incorporated into a sloping system, but its contribution must be separated from gravity by comparing matched total hydraulic head conditions. Begin with a level floor to isolate geometry. Whole-inch height rounding also turns ideal continuous grades into a stepped numerical bed.
6. **Validation claims exceeded the checks.** Valid PNGs, valid JSON schemas and calculated centerline lengths do not establish fluidic-diode performance. The previous raster maps were not validated as Tesla valves.

## The geometry and physics to preserve

The patent contains repeated, asymmetric hooked passages, long inclined walls, solid elongated islands and narrow mouths. The stages alternate across a comparatively direct central corridor. The forward and reverse configurations are the same physical geometry; only the driving pressure direction changes.

In reverse, the wall/island encounters produce more diversion, turning, separation, recirculation and mixing. These losses depend on flow regime. It is not sufficient to add an arbitrary loop, choose a universal 45-degree angle or insist that every parcel follows an arrow. Preserve the mouth orientation, divider shape, clearance, bend and re-entry together.

Nguyen et al. experimentally tested a conduit digitized from Tesla's patent. They found rectification activating near Reynolds number 200 in their apparatus and diodicity around 2 over Re 300–1500. Those are apparatus-specific results, not universal thresholds or evidence of an ideal check valve. Reverse flow remains possible.

The sandbox's staggered MAC model includes depth-averaged momentum and optional advection. It can represent useful horizontal inertial effects, but its roughness/damping and coarse grid do not establish faithful reproduction of the closed conduit's three-dimensional viscous/turbulent losses. Four one-foot cells across a four-foot channel leave too little room to preserve narrow mouths and rounded return details. A finer mesh and a convergence study are needed before trusting a resistance ratio.

## Drawing

[tesla-valve-reference.png](tesla-valve-reference.png) shows an idealized patent-based top-down schematic twice, with forward and reverse drive indicated. It also includes the original patent planform for comparison. It is a geometry reference, not a dimensioned fabrication drawing, a four-foot-scale map export or a simulated flow result. Light blue is the fluid passage; dark gray is solid material.

The reference deliberately uses a level floor. Scaling it to a four-foot minimum clear passage requires measuring every constriction and then choosing a grid fine enough to retain those clearances. Do not force six stages into the existing grid by shrinking away the dividers.

## A meaningful next validation

Build one resolved stage first and confirm passage connectivity, divider thickness, mouth orientation and actual clear widths on the raster. Use equal-magnitude forward and reverse head differences with identical wetting and boundary conditions, advection enabled, and walls that cannot be overtopped. After transients settle, compare forward/reverse discharge at equal head, or reverse/forward head loss at equal discharge. Check mass balance and repeat at finer resolution. Only then repeat the stage and add the desired terrain grade. Small poured packets remain useful for visualization, but are not by themselves this measurement.

## Sources and image attribution

- Nikola Tesla, [US1329559A, Valvular conduit](https://patents.google.com/patent/US1329559A/en), 1920.
- Nguyen et al., [Early turbulence and pulsatile flows enhance diodicity of Tesla's macrofluidic valve](https://www.nature.com/articles/s41467-021-23009-y), Nature Communications 12, 2884 (2021), DOI 10.1038/s41467-021-23009-y.
- `tesla-source-figure.png` is the latter article's Figure 1, including its reproduction of the patent drawing. The article is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The reference PNG crops/resizes panel a and adds a new idealized schematic and explanatory text. No figure is presented as a new experiment.
- `draw_tesla_reference.py` regenerates the reference PNG using Matplotlib and Pillow; it downloads the attributed source figure if absent.
