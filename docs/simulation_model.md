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
- control gates

Ancient infrastructure typically defines:

- water containment
- flow direction
- pressure management
- structural load distribution

### Modern Modifications

Modern additions are local adaptations built by present-day inhabitants.

Examples:

- wooden braces
- sandbag barriers
- crude sluice gates
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