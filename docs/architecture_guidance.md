# Architecture Guidance

## Goal
Support a simulation-forward game whose systems are inspectable, extensible, and easy to iterate on.

## Recommended Conceptual Modules
- `WorldState`
- `RegionState`
- `InfrastructureNode`
- `FlowNetwork`
- `SettlementState`
- `Intervention`
- `ConsequenceSimulator`
- `MagicInfluence`
- `KnowledgeModel`
- `EventLog`
- `DebugOverlay`

## Tile Representation

Map cells are not single tile identities.
Instead each cell stores layered state components.

Example layers:

terrain_layer
infrastructure_layer
water_layer
condition_layer
ecology_layer
arcane_layer

Each layer contributes properties used by the simulation engine.

## Data Priorities
Prefer explicit state over opaque side effects.

Useful data patterns:
- graphs for infrastructure dependencies
- time-stepped updates
- per-region metrics
- intervention history logs
- event causality notes

## Visualization Priorities
Need early:
- map overlay for water stress / saturation / flow
- highlighting of infrastructure links
- before/after metrics
- visible downstream consequence markers
- simple in-world or debug explanation of why a change happened

## Vertical Slice Bias
The first build only needs enough architecture to support:
- 1 region
- 2-3 settlements
- several infrastructure nodes
- a few intervention types
- visible time progression
- clear consequences

Avoid overengineering generalized RPG systems too early.

## Layered Infrastructure Representation

Tiles are not represented as a single object.

Instead, each map cell contains layered state components.

Example cell structure:

terrain_layer  
ancient_infrastructure_layer  
modern_modification_layer  
water_layer  
condition_layer  
ecology_layer  
arcane_layer

Each layer contributes properties to the simulation.

Final tile behavior emerges from the aggregation of those properties.

### Example Data Representation

Example conceptual structure:

'''
TileState
terrain: TerrainType
ancient_structure: AncientStructureType
modern_modification: ModificationType
water_flow: FlowState
condition: StructuralIntegrity
ecology: VegetationState
arcane: ArcaneInfluence
'''

Simulation systems read these values and compute outcomes such as:

- leakage
- flow redirection
- flooding
- desertification
- ecological changes
- structural collapse

This layered approach supports emergent world behavior and reduces the need for rigid tile type definitions.