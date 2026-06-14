# Transaction History

Archived completed transactions.

---

## TRANSACTION: TX-0001

STATUS: COMPLETE
CREATED: 2026-03-08T18:40:00Z
COMPLETED: 2026-03-08T19:05:00Z

AUTHOR: user

DESCRIPTION:
Add a "Modern Modifications" layer to the world simulation model.

This represents repairs, improvisations, and local infrastructure hacks
built by current civilizations on top of ancient systems.

TARGET FILES:

- docs/simulation_model.md
- docs/world_systems.md
- docs/game_vision.md

CHANGES:

### docs/simulation_model.md

Add a new simulation layer called "Modern Modification".

This layer represents local adaptations to ancient infrastructure such as:

- wooden braces
- crude sluice gates
- sandbag flood barriers
- patched canal leaks
- bypass trenches

These modifications contribute properties including:

- temporary containment
- altered water flow
- structural reinforcement
- new failure risks

### docs/world_systems.md

Add a section titled:

"Modern Adaptations to Ancient Infrastructure"

Describe how communities improvise repairs and workarounds to ancient
infrastructure they do not fully understand.

Include examples such as:

- wooden flumes attached to ancient canals
- settlements tapping ancient water pressure systems
- patched aqueduct cracks
- ritualized infrastructure nodes

### docs/game_vision.md

Add a paragraph explaining that the modern world is shaped by:

ancient infrastructure  
+ centuries of improvised modifications

NOTES:
None yet.

---

## TRANSACTION: TX-0002

STATUS: COMPLETE
CREATED: 2026-03-08T18:50:00Z
COMPLETED: 2026-03-08T19:05:00Z

AUTHOR: user

DESCRIPTION:
Define layered tile model for world simulation.

TARGET FILES:

- docs/simulation_model.md
- docs/architecture_guidance.md

CHANGES:

### docs/simulation_model.md

Add section titled:

"Layered Tile State Model"

Explain that each map cell contains multiple simulation layers:

- terrain
- ancient infrastructure
- modern modification
- water state
- structural condition
- ecology
- arcane influence

Tile behavior emerges from combined properties.

### docs/architecture_guidance.md

Add explanation that map cells should not be represented by a single tile type.

Instead, a tile state should aggregate layered components.

Example conceptual structure:

TileState
- terrain
- ancient_structure
- modern_modification
- water
- condition
- ecology
- arcane

NOTES:
This supports additive physical behavior and emergent environmental changes.

---

## TRANSACTION: TX-TEST-0002

STATUS: COMPLETE
CREATED: 2026-03-08T20:15:00Z
COMPLETED: 2026-03-09T00:15:00Z
EXECUTED_BY: Codex

AUTHOR: user

DESCRIPTION:
Add "water pressure" to simulation properties.

TARGET FILES:

- docs/simulation_model.md

CHANGES:

### docs/simulation_model.md

Added a "Water Pressure" section that explains how pressure signals push
against containment, what conditions raise it, and how increasing pressure
feeds leaks, structural failures, and redirected flows.

NOTES:
Simulation expansion test.

---

## TRANSACTION: TX-TEST-0003

STATUS: COMPLETE
CREATED: 2026-03-08T20:20:00Z
COMPLETED: 2026-03-09T00:20:00Z
EXECUTED_BY: Codex

AUTHOR: user

DESCRIPTION:
Add "Canal Inspectors" profession to worldbuilding.

TARGET FILES:

- docs/world_systems.md

CHANGES:

### docs/world_systems.md

Created a "Canal Inspectors" subsection that introduces traveling specialists
who map leaks, read flow patterns, predict downstream shifts, and advise on
repairs based on folk methods and partial ancient records.

NOTES:
Worldbuilding profession test.

---

## TRANSACTION: TX-0003

STATUS: COMPLETE
CREATED: 2026-03-18T17:30:00Z
COMPLETED: 2026-03-18T18:10:00Z
EXECUTED_BY: Codex

AUTHOR: user

DESCRIPTION:
Adopt Fluidic / No-Moving-Parts Ancient Technology Model.

TARGET FILES:

- AGENTS.md
- docs/ancient_technology.md
- docs/fluidic_logic.md
- docs/ruins/tide_logic_regulator.md
- docs/simulation_model.md
- docs/architecture_guidance.md
- experiments/tide-logic-regulator-002/simulator.h
- experiments/tide-logic-regulator-002/simulator.cpp
- experiments/tide-logic-regulator-002/dashboard.cpp

CHANGES:

### Canonical Model

Defined ancient technology as fluidic, geometric, passive, and pressure-mediated by default, with no moving parts as the core assumption.

### Documentation

Added dedicated documents for ancient technology, fluidic logic, and a fluidic Tide Logic Regulator ruin model.

Updated simulation and architecture guidance to prefer pressure, siphons, air pockets, basins, and geometry over gate-style abstractions.

### Experiment 2 Refactor

Refactored the Tide Logic Regulator prototype away from gate logic and toward:

- pressure comparison
- delay-basin memory
- air-trap behavior
- threshold lips
- siphon state transitions
- spillway overflow

### Rationale

This better matches ten-thousand-year survivability, makes degradation produce miscomputation instead of simple failure, and increases exploration value by making hidden structure legible through environmental behavior.

NOTES:
Legacy gate-style behavior in experiment 2 was replaced by siphon-state and fluidic-control modeling rather than deleted without replacement.

---

## TRANSACTION: TX-0004

STATUS: COMPLETE
CREATED: 2026-03-18T19:00:00Z
COMPLETED: 2026-03-18T19:35:00Z
EXECUTED_BY: Codex

AUTHOR: user

DESCRIPTION:
Integrate layered ancient civilization model, Part 1/Part 2 progression, and passive fluidic world canon.

TARGET FILES:

- AGENTS.md
- docs/world/ancient_civilization.md
- docs/world/historical_layers.md
- docs/world/knowledge_geography.md
- docs/world/transport_and_megastructures.md
- docs/world/part1_part2_structure.md
- docs/systems/repair_philosophy.md
- docs/systems/hidden_full_simulation.md
- docs/ruins/late_civilization_cathedral_layer.md
- docs/lore/archaeological_ambiguity.md
- docs/game_vision.md
- docs/core_loops.md
- docs/roadmap.md

CHANGES:

### Ancient Civilization Thesis

Documented that the ancient civilization solved so many needs through passive infrastructure that it lost the pressure to renew its deepest knowledge, rather than failing because of primitiveness.

### Passive Fluidic Canon

Reinforced landscapes computing with water as the core technology model and tied that model to transport, environmental regulation, and social equilibrium.

### Transport And Megastructures

Clarified that giant canals and aqueducts were full transport systems and that water, gradient, and time replaced engines, roads, and cranes.

### Knowledge Geography And Historical Layers

Defined the mountain source layer, lowland inherited layer, early-versus-late civilization distinction, and the tension between visible culture and hidden infrastructure.

### Part 1 / Part 2 Structure

Documented Part 1 as lowland adaptation to instability and Part 2 as deeper causal correction, both operating within one hidden full simulation.

### Exploration Reward Model

Strengthened knowledge progression, reinterpretation, and better causal models as primary exploration rewards.

NOTES:
The update was integrated as durable canon docs and guidance rather than speculative code.

---

## TRANSACTION: TX-0005

STATUS: COMPLETE
CREATED: 2026-03-18T20:00:00Z
COMPLETED: 2026-03-18T20:25:00Z
EXECUTED_BY: Codex

AUTHOR: user

DESCRIPTION:
Refactor coastal pump canon into a sea-fed, siphon-triggered two-stroke packet pump.

TARGET FILES:

- docs/ruins/sea_fed_two_stroke_packet_pump.md
- docs/ruins/sea_fed_two_stroke_packet_pump_cycle.svg
- docs/systems/fluidic_logic.md
- docs/physical_ruin_modeling.md

CHANGES:

### Canonical Refactor

Replaced tide-centered framing for this machine family with a more general sea-fed pulse model built on slow charge, sudden discharge, and staged packet lift.

### Core Constraints

Documented saltwater as power source, freshwater as payload, and packet isolation between stages as the core physical design rule.

### Design And Gameplay Value

Added canonical architecture, operating cycle, prototype sequence, failure modes, and gameplay-oriented repair implications.

### Diagram Support

Added an SVG operating-cycle diagram for future concept, prototype, and worldbuilding use.

NOTES:
The canonical sentence is now: a sea-fed, siphon-triggered, two-stroke packet pump that lifts freshwater uphill in isolated stages using mostly static hydraulic geometry rather than mechanical valves.
