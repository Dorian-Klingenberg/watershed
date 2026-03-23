# Tide Logic Regulator

## Canonical Interpretation

The Tide Logic Regulator is a coastal fluidic release computer.

It is not a mechanical gatehouse.
Its behavior emerges from:

- reservoir head
- tide backpressure
- a pressure-comparison chamber
- a delay basin
- an air-trap chamber
- a threshold lip
- a siphon release path
- an overflow spillway

State diagram:

- [tide_logic_regulator_state_diagram.svg](/D:/Repos/Games/TheGame/docs/ruins/tide_logic_regulator_state_diagram.svg)

## Physical Description

Physically, the ruin would not look like a machine room full of mechanisms.
It would look like a layered coastal stonework complex built into a head difference between inland storage and tidal outflow.

### Overall Layout

The core ruin would likely have five linked elevations:

1. an inland holding basin set slightly above the surrounding floodplain
2. a carved comparison terrace where inland pressure and tidal backpressure are both sampled
3. a side delay basin cut into bedrock or lined masonry
4. a raised siphon throat with a carefully shaped threshold lip and air-trap cavity
5. a lower discharge course leading seaward, plus a separate spillway descending into marsh

### Reservoir Branch

The inland branch would be broad, heavy, and conservative.
It would probably be:

- a stone-lined feeder canal
- a settling forebay
- a storage basin with thick retaining walls

This branch exists to create stable pressure head, not to perform delicate logic itself.

### Tide Branch

The tide branch would be narrower and easier to foul.
It would likely be a low tunnel or culvert that transmits downstream backpressure toward the comparator terrace.
Because it is narrow, silt, shell accumulation, and reed-root intrusion can corrupt the sensed tide without fully blocking the sea.

### Pressure Comparator

The comparator is best imagined as a carved chamber where two water surfaces influence the same intermediate pocket through differently shaped inlets.
It does not "switch."
Instead, its geometry determines whether inland pressure is winning strongly enough to feed the next stage.

Useful physical features here:

- asymmetric inlet heights
- a narrowed throat on the inland side
- a calmer sensing pocket on the tide side
- inspection shafts or sounding wells above the chamber

### Delay Basin

The delay basin would be a side cistern slightly above or beside the comparator.
It fills only when the comparator is favorable, then drains slowly through a restricted return path.
That makes it the ruin's memory element.

In ruins, this would likely appear as:

- a plastered side basin
- mineral tide marks on the walls
- a narrow bleed channel cut back toward the comparator or siphon throat

### Air Trap Chamber

The air trap would sit at or just before the siphon crest.
Its job is to capture, compress, or vent air so the siphon can either sustain or fail.
Physically, this could be a bulb-like overhead cavity in stone with a narrow vent fissure or service shaft.

This is one of the most important "impossible-looking" features in the ruin because a later culture could easily mistake it for decorative void space and accidentally spoil the system by patching it.

### Threshold Lip And Siphon Path

The threshold lip is a shaped stone sill.
Once inland head and comparator output together reach it, water can begin climbing into the siphon throat.
Beyond that lip, the discharge path bends over a crest and drops into the seaward channel.

Physically, the siphon path would likely be:

- enclosed or partially enclosed
- smooth compared with ordinary channels
- difficult to inspect directly
- extremely sensitive to cracks, air leaks, and slight settlement

### Overflow Spillway

The spillway would be visibly secondary but structurally massive.
It would descend toward marsh or sacrificial wetland rather than toward the main discharge route.
That makes it easy for later inhabitants to misread as wasteful loss and wall off.

In practice it protects:

- the reservoir terrace from overpressure
- the comparator from distorted loading
- the siphon throat from chaotic surges

### How The Ruin Reads To A Player

A player exploring the ruin should not see obvious moving hardware.
They should instead read function from:

- water marks at different heights
- salt lines in the tide branch
- smooth versus rough stone in flow-critical channels
- trapped-air cavities and vent shafts
- marsh growth around the spillway
- subtle lip heights and branch asymmetries

The mystery is architectural and hydraulic, not mechanical.

## Normal Operation

1. Inland runoff raises reservoir head.
2. Tide branch reports coastal backpressure.
3. If inland head exceeds the threshold lip and tide backpressure is low enough, the comparator feeds the delay basin.
4. The delay basin keeps priming conditions alive long enough for the siphon path to catch and sustain.
5. The air trap remains stable, so the siphon stays active.
6. If head grows too high, the spillway diverts excess into marshland.

This behaves like a fluidic AND:

- inland pressure must be ready
- tide backpressure must be safe

## Degraded States

### Silted Tide Branch

- sensed tide lags the real tide
- the ruin believes release is safer than it is
- the siphon can stay active into hostile backpressure

### Leaking Reservoir Branch

- true inland water exists, but effective head is reduced
- the siphon may fail to prime
- downstream channels go dry while wetlands spread elsewhere

### Cracked Delay Basin

- stored memory drains too fast
- the regulator chatters between priming and loss of prime

### Unstable Air Trap

- air intrusion increases
- the siphon breaks prematurely

### Geometry Drift

- lip thresholds move
- timing changes
- a stable release window becomes systematically wrong

## Miscomputation Scenarios

### False Safe Release

The tide branch is delayed by sediment, so the siphon primes and sustains even though the real tide is already too high.

### Delayed Release Collapse

The delay basin keeps enough charge to preserve a bad state, but air intrusion arrives later and breaks the siphon during peak inland pressure.

### Local Fix, Regional Error

Sealing the reservoir leak improves inland head and downstream delivery.
It also makes false-safe priming happen more often and can overwhelm the spillway.

### Restored Memory, Worse Decisions

Repairing the delay basin creates beautiful stable cycles.
If the comparator is already wrong, the ruin now makes the wrong decision more cleanly and for longer.
