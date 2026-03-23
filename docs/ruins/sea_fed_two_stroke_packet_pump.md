# Sea-Fed Two-Stroke Packet Pump

## Purpose

This document defines the canonical sea-fed packet-lift machine for coastal water systems.

This concept replaces older, more tide-dependent framings for this family of devices.
It should be treated as stable unless explicitly superseded.

## Canonical Terminology

Preferred names:

- sea-fed pulse pump
- two-stroke packet pump
- staged packet-lift hydraulic system
- ocean-charged hydraulic oscillator

Avoid overcommitting to:

- tide machine
- tidal engine
- tide-powered pump

Those labels may still describe some in-world installations, but they are no longer the general or preferred framing.

## Core Design Statement

The machine is a sea-fed, siphon-triggered hydraulic pulse system that uses saltwater as an energy source to lift freshwater uphill in discrete isolated packets, with directionality produced primarily by geometry rather than moving valves.

## Foundational Principles

### Saltwater Is Power; Freshwater Is Payload

The sea side provides:

- inflow
- stored head
- hydraulic pulse energy

The freshwater side provides:

- intake packets
- staged lift
- upper storage for settlement use

These must remain conceptually separate in docs, diagrams, and prototypes.

### Do Not Lift One Giant Continuous Column

This is a hard constraint.

The system must not be designed as one long flooded lift shaft whose entire weight must be overcome every cycle.

Instead:

- each stage lifts only a local packet
- each stage captures a modest local gain
- each stage then re-isolates
- future cycles do not need to carry the full mass already gained

### Pressure Lifts; Geometry Keeps The Gain

The pulse only needs to create temporary dynamic lift.

Permanent retained elevation comes from:

- perched basins
- spill lips
- isolated receiving chambers
- de-priming geometry
- air breaks
- vents

### Geometry Replaces Valves

The machine should be biased toward:

- minimal moving parts
- preferably no mechanical valves in the core cycle
- carved-stone hydraulic logic

Useful geometry includes:

- perched transfer basins
- overflow sills
- short local risers
- standpipes
- siphon triggers
- vent shafts
- air breaks
- de-priming notches
- baffled pools
- offset receiving chambers

### Each Stage Must Re-Isolate

After a packet spills upward:

- the stage must stop acting like a continuous column
- the higher packet must remain locally retained
- the next pulse must not have to lift previously gained water again

This is one of the machine's most important physical constraints.

## Canonical Architecture

### A. Sea-Fed Charge Chamber

A chamber that fills slowly from the sea through a restricted inlet.

Purpose:

- accumulate hydraulic potential
- act as a pulse capacitor

### B. Trigger Siphon

A siphon that self-starts when the charge chamber reaches a set height.

Purpose:

- convert slow fill into rapid discharge
- create automatic cyclic behavior

### C. Pulse Coupling Chamber

A chamber that experiences the useful transient from rapid discharge.

Purpose:

- produce suction and or pressure variation
- couple sea-side pulse energy to freshwater lifting work

### D. Freshwater Intake Pocket

A local intake chamber connected to freshwater supply.

Purpose:

- receive one discrete packet during the intake phase

### E. Short Packet Riser

A short local vertical path to the next elevation.

Purpose:

- allow modest local lift per stage

### F. Perched Capture Basin

A higher chamber or basin that receives overflow from the riser.

Purpose:

- store gained elevation
- become the next stage's starting point

### G. Vent Or Air Break

A vented feature that prevents the system from behaving like one continuous hydraulic column.

Purpose:

- isolate gains between cycles
- force packet behavior
- support de-priming

## Canonical Cycle

### Phase 1: Slow Charge

Sea water enters through a restricted inlet and slowly fills the charge chamber.

### Phase 2: Siphon Trigger

When the charge chamber reaches the siphon crest height, the siphon catches.

### Phase 3: Rapid Discharge

The siphon rapidly empties the charge chamber, producing the main transient event.

### Phase 4: Intake Stroke

That transient produces suction and or a favorable pressure difference that draws a discrete freshwater packet into the intake pocket.

### Phase 5: Return Or Lift Stroke

As the transient evolves or resets, the freshwater packet is pushed up a short local riser.

### Phase 6: Capture

The packet spills into a perched higher basin.

### Phase 7: Isolation

Vent and basin geometry prevent the packet from falling back and prevent the system from acting like one giant column.

### Phase 8: Reset

The stage is ready to repeat.

## Gameplay Implications

This device is attractive because it is:

- understandable in stages
- explorable physically
- repairable modularly
- visually expressive
- mechanically legible

Good gameplay around it includes:

- diagnosing stage failures
- restoring siphon priming
- clearing blocked vents
- re-establishing packet isolation
- repairing worn spill lips
- desilting intake pockets and perched basins
- preventing saltwater contamination across sides

## Failure Modes

### Hydraulic And Prototype Failures

- siphon fails to prime
- siphon fails to break cleanly
- air leak weakens the pulse
- chamber volumes are mismatched
- packet is captured but not retained
- backflow occurs after transfer
- vent fails to isolate the stage
- geometry accidentally creates one large continuous water column
- stage height is too ambitious
- discharge is too weak or too damped

### World And Gameplay Failures

- spill lip worn down by erosion
- sediment reduces effective basin volume
- intake throat clogged
- trapped-air path flooded
- de-priming notch blocked
- saltwater breaches freshwater side
- structural crack prevents isolation
- upper basin overflows or leaks into the previous stage

## Prototype Strategy

Prototype sequence:

1. sea-fed pulse generator
2. pulse coupling test
3. freshwater intake proof
4. single packet-lift stage
5. multi-stage stack

The key questions are always:

- does the siphon trigger reliably
- is the pulse sharp enough to matter
- does a real packet get captured
- does a stage retain gain independently
- where does accidental full-column coupling reappear

## Worldbuilding Value

This system supports ancient tall coastal cities because:

- power extraction happens low near sea access
- freshwater can be lifted incrementally
- upper districts can remain high above surge and flooding
- gravity-fed distribution from high cisterns becomes possible
- architecture itself becomes infrastructure

The walls, shafts, and basins are the machine.
