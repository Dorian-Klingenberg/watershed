# Simulation Model

## Core Principle

The world is simulated using layered tile states.
Each map cell may contain multiple overlays that contribute
physical and systemic properties.

Final tile behavior emerges from the combination of those properties.

## Tile Layers

Terrain
Infrastructure
Water
Condition
Ecology
Arcane Influence

## Ancient Technology Assumption

Ancient infrastructure should be modeled as fluidic, geometric, and passive by default.

Prefer:

- pressure head
- siphon state
- basin storage
- air pockets
- threshold lips
- flow-bias geometry

Do not assume ancient control relies on moving gates or actuator-style valves unless a document explicitly justifies an exception.

## Example Tile State

terrain: clay
infrastructure: canal
integrity: 0.4
water_flow: medium
vegetation: reeds

## Derived Effects

leakage: moderate
saturation: increasing
erosion_risk: medium
canal_efficiency: reduced

### Water Pressure

Water pressure represents how strongly water attempts to move through infrastructure.
It reflects the force pushing against containment structures and influences subsequent
simulation outcomes.

Pressure increases when:

- upstream water volume rises
- canals narrow
- flow is blocked

High pressure contributes to:

- leaks
- structural failures
- redirected flows

Monitoring water pressure helps highlight hidden stresses before catastrophic failures.

### Fluidic Signal State

Pressure is not only a physical force.
It is also a signal.

Ancient ruins often compute through:

- pressure differentials
- delayed basin filling
- siphon priming and breakage
- air-trap stability
- overflow activation

### Required Tile Primitives

Simulation layers should be able to express:

- `TileWaterLevel`
- `TilePressureHead`
- `TileAirVolume`
- `TileFlowResistance`
- `TileSiphonState`

Useful `TileSiphonState` values:

- `unprimed`
- `priming`
- `active`
- `broken`

## Simulation Loop

1. update water flow
2. compute containment failures
3. update saturation
4. update ecology
5. update infrastructure decay

## Infrastructure Layers

Each map cell can contain multiple interacting layers.

These layers contribute properties that combine to produce the tile's behavior.

Core layers include:

1. Terrain
2. Ancient Infrastructure
3. Modern Modification
4. Water / Flow State
5. Structural Condition
6. Ecology
7. Arcane Influence

### Terrain

Defines base environmental properties such as:

- permeability
- erosion resistance
- fertility
- slope

Examples:

clay  
sand  
bedrock  
loam  
ancient paving

### Ancient Infrastructure

Represents original structures from the lost civilization.

Examples:

- canal segments
- aqueduct walls
- pressure nodes
- reservoirs
- siphon channels
- threshold lips
- delay basins
- air-trap chambers
- overflow spillways
- flow-bias branches

Ancient infrastructure typically defines:

- water containment
- flow direction
- pressure management
- structural load distribution
- air management
- timing and memory behavior

### Modern Modifications

Modern additions are local adaptations built by present-day inhabitants.

Examples:

- wooden braces
- sandbag barriers
- crude shutters or temporary barriers
- patched leaks
- bypass trenches
- scavenged ancient components

These modifications contribute properties such as:

- temporary containment
- altered flow direction
- structural reinforcement
- leak reduction
- new failure risks

Modern modifications may stabilize ancient structures or introduce additional fragility.

### Interaction Example

A single tile might contain:

terrain: clay  
ancient infrastructure: canal wall  
condition: cracked  
modern modification: timber brace  
water pressure: medium  

Derived results:

- leakage: low but increasing
- structural integrity: unstable
- downstream flow: reduced
- saturation of nearby terrain: moderate

These combined states determine the tile's simulation behavior.

## Failure As Miscomputation

Ancient systems should usually not transition from "working" to "broken."

They should drift into incorrect behavior through:

- sediment accumulation
- leak growth
- air intrusion
- geometry drift

Those changes alter timing, thresholds, and branch priority.
The interesting failure is usually a ruin that still computes, but computes the wrong answer.
