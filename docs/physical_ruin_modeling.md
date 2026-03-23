# Physical Ruin Modeling

## Purpose

This document defines how a ruin can be:

- numerically simulated for world state
- physically representable in 3D
- plausibly buildable in real life
- partially explorable and repairable by players

The goal is not to run the whole world as full 3D fluid physics.
The goal is to ensure every important ruin is grounded in a coherent physical design language that can support both systemic simulation and local adventure spaces.

## Core Principle

The simulation may be abstract.
The design must be concrete.

That means:

- the game state can use reduced numeric models
- each ruin should still correspond to believable physical structures
- every major hydraulic function should have a plausible spatial form

If a structure cannot be reasonably imagined as a built place with elevations, chambers, conduits, access routes, and failure surfaces, it is probably too abstract.

## Three Linked Layers

Each ruin should be thinkable at three connected layers.

### 1. System Layer

This is the gameplay and simulation model.

Track things like:

- water level
- pressure head
- flow resistance
- air volume
- sediment load
- leak rate
- siphon state
- threshold height
- overflow activation

This layer is enough to drive global world state.

### 2. Physical Layer

This is the blueprint interpretation.

Describe:

- elevation relationships
- channel widths and lengths
- chamber volumes
- wall thickness
- lip heights
- vent locations
- inspection shafts
- spillway geometry
- maintenance routes

This layer answers:

**what is this ruin physically made of, and how does it achieve the simulated behavior?**

### 3. Explorable Layer

This is the player-facing local space.

Only some ruins need to be instantiated at this layer.
When they are, the player should be moving through parts of the same physical system rather than a disconnected puzzle shell.

This layer includes:

- walkable ledges
- climb shafts
- flooded tunnels
- dry galleries
- dangerous drops
- blocked culverts
- repair surfaces
- observation points

## Modeling Rule

Every systemic component should have a plausible physical embodiment.

Examples:

- `DelayBasin` becomes a cistern, side chamber, or elevated settling pocket
- `AirTrapChamber` becomes an overhead bulb cavity, vent shaft, or trapped dome
- `ThresholdLip` becomes a carved sill or crest edge
- `SiphonChannel` becomes an enclosed conduit over a crest and down a drop
- `FlowBiasChannel` becomes asymmetric branching geometry
- `OverflowSpillway` becomes a robust sacrificial descent path into marsh, basin, or floodplain

## What "Buildable In Real Life" Means

This does not mean the ruin must be engineered to modern standards.
It means the structure should feel like something stone-and-water engineers could actually lay out and construct.

A ruin should therefore obey:

- gravity
- pressure relationships
- finite volume
- plausible access for inspection or maintenance
- material constraints
- spatial continuity

Avoid magical-feeling infrastructure that only works as symbolic game logic.

## Preferred Hydraulic Primitives

These are the core primitives that should be reusable across ruins.

### Basins

Used for:

- storage
- timing
- settling
- memory

Spatial forms:

- cisterns
- terrace pockets
- side chambers
- carved rock bowls

### Channels

Used for:

- conveyance
- sensing
- biasing
- controlled loss

Spatial forms:

- open cuts
- lined canals
- culverts
- narrow stone throats
- enclosed conduits

### Lips And Crests

Used for:

- threshold behavior
- overflow initiation
- siphon priming conditions

Spatial forms:

- carved sill edges
- worn stone notches
- raised floor transitions

### Air Structures

Used for:

- siphon sustain
- siphon breakage
- timing
- instability

Spatial forms:

- vent shafts
- dome pockets
- trapped ceiling cavities
- cracked high chambers

### Spillways

Used for:

- structural protection
- sacrificial routing
- ecological side effects

Spatial forms:

- stepped descents
- marsh feeders
- broad overflow aprons
- bypass gullies

## Local Exploration Design

When a ruin becomes explorable, the player should be able to understand it through movement and observation.

Useful readable features:

- mineral lines marking past water heights
- smoother stone where flow was persistent
- root intrusion where seepage exists
- salt deposits near tidal sensing branches
- collapsed ceilings caused by pressure misrouting
- vent shafts that explain air behavior
- side basins with visible charge and drain relationships
- perched capture basins that show packetized lift rather than continuous pumping

The player should be able to infer function from architecture.

## Repair Interaction Principles

Repairs should act on physical causes, not abstract switches.

Good interaction targets:

- clear sediment from a sensing culvert
- reopen a vent shaft
- patch a crack in a delay basin
- remove a later wall blocking a spillway
- prime a siphon manually
- restore packet isolation between stages
- repair a worn spill lip that no longer retains a gained packet
- measure head differences across chambers
- reroute temporary flow through bypass trenches

For packet-lift machines, also enforce this constraint:

- do not let a repair accidentally re-couple the whole stack into one continuous water column

Bad interaction targets:

- pull a lever to enable logic
- toggle a valve-state puzzle
- insert a generic key into an ancient machine slot

## Global Simulation Versus Local Instantiation

The world simulation only needs reduced-order behavior.

For most ruins:

- use graph or tile abstractions
- use continuous quantities
- update through time-stepped numeric rules

For selected ruins:

- instantiate part of the physical layout spatially
- let player actions modify the same underlying simulation variables

This is the bridge between large-scale systems and adventure gameplay.

## Authoring Checklist

When designing a new ruin, answer these questions:

1. What are its core hydraulic functions?
2. What primitive structures implement those functions?
3. What are the key elevations?
4. Where are the thresholds?
5. Where can air accumulate or vent?
6. What forms of degradation change behavior without fully stopping it?
7. Which parts are safe or possible for a person to explore?
8. Which local interventions map back into global state changes?

If these questions cannot be answered concretely, the ruin design is not ready.

## Canonical Standard

The target is:

- abstract enough to simulate at world scale
- concrete enough to draw, model, and explain in 3D
- physical enough that a player can explore and repair part of it
- systemic enough that local fixes create distant consequences

That combination is the project's ideal form of ancient infrastructure.
