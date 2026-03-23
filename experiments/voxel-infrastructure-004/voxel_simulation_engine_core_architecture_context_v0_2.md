# Voxel Simulation Engine - Core Architecture Context (v0.2)

## Purpose

This document captures the current architecture direction for the voxel engine / simulation substrate project so future Codex discussions can start from a coherent state.

The focus is now explicitly on:

* voxel rendering as a simulation substrate, not just a visual style
* CPU-owned world generation and streaming
* GPU-resident active world-state caching
* memoization / cached derived structure as a major optimization strategy
* incremental, additive simulation state instead of full overwrite / full recompute
* careful tradeoffs between compute vs lookup and CPU vs GPU

---

## High-Level Vision

Build a voxel-based environmental simulation engine that can support:

* hydrology
* erosion
* soil science / saturation
* geothermal interactions
* structural stress / collapse
* ancient mechanisms / infrastructure
* procedural terrain and ruins
* later: agents interacting with fields and structures

The engine is not merely a voxel renderer.
It is a persistent spatial simulation framework that happens to use voxels as its primary substrate.

---

## Core Philosophy

### 1. Voxels are the shared substrate

Voxels are attractive not just because cubes are simple, but because they give:

* discrete space
* easy neighborhood relationships
* easy material lookup
* easy destruction / excavation / modification
* easy chunking / streaming
* natural support for scalar fields

This lets many experiments live in one engine instead of being built as disconnected prototypes.

### 2. The engine should unify experiments

The evolving voxel engine should host:

* graphics experiments
* simulation experiments
* worldgen experiments
* destruction experiments
* infrastructure experiments

Instead of making one demo per idea, use one shared engine and one shared world representation.

### 3. The engine should be evolvable, not gigantic

Do not try to build the whole dream game at once.
Instead:

* stable core engine
* modular simulation systems
* focused experiment scenes
* later convergence into gameplay

---

## World Representation

### Authoritative CPU World

The CPU owns the authoritative world state.

This includes:

* chunked voxel grid
* procedural generation from seed
* sparse edits / persistence
* chunk streaming / residency management
* active-region tracking
* irregular simulation logic

Possible per-voxel or per-cell baseline data:

* `materialId`
* `solidity` or `density`
* `flags`

The CPU is responsible for deciding what exists in the world.
The GPU is primarily responsible for presenting and sampling the active working set.

---

## Scalar Field Mindset

A major reason to build on a voxel substrate is that it naturally supports 2D and 3D scalar fields.

Examples:

* density
* temperature
* moisture
* pressure
* salinity
* fertility
* erosion susceptibility
* geothermal gradient
* contamination
* structural stress
* any game-specific field later

This means the engine is not only a terrain engine. It is a general spatial field engine.

Many systems share the same computational pattern:

* store value over space
* update from neighbors
* apply sources / sinks
* visualize for gameplay and debug

This common shape gives huge reuse across systems.

---

## Simulation State Types

A key distinction emerged in the conversation.

### 1. Instantaneous State

State that describes what is true right now.

Examples:

* current water amount
* current temperature
* current moisture
* current support load

### 2. Process Memory / Accumulated State

State that stores the progress of processes over time.

Examples:

* `erosionDebt`
* `sediment`
* `crackDamage`
* `fatigue`
* `heatSoak`
* `flowPreference`
* `saturationHistory`
* `recentStress`
* `timeSinceFlooded`

This second category is especially important.
It allows the world to remember what has happened there.

This is what makes the simulation path-dependent rather than purely stateless.

---

## Incremental / Additive Simulation

A core idea is to avoid full recomputation and full overwrite where possible.

Target model:

* keep persistent local state
* apply additive or delta updates
* only touch dirty regions
* trigger topology changes at thresholds

Not:

* recompute everything every tick
* overwrite whole fields every frame

Examples:

* erosion accumulates gradually until a voxel is removed
* crack damage accumulates until fracture occurs
* sediment accumulates until deposition changes topology
* heat soak accumulates and influences later behavior

This approach should support:

* lower cost
* richer world memory
* more believable environmental history
* better emergence

---

## Event Threshold Model

A useful pattern is:

1. continuous fields and accumulated process state evolve incrementally
2. discrete events happen only when thresholds are crossed

Examples:

* `erosionDebt > threshold -> remove soil voxel`
* `crackDamage > threshold -> fracture or weaken`
* `saturation + weak material -> collapse`
* `pressure buildup > threshold -> release / redirect / vent`

This separates slow process evolution from occasional structural edits.

---

## CPU vs GPU Responsibilities

### CPU Responsibilities

The CPU is a good fit for:

* procedural terrain generation
* chunk loading / unloading
* persistence / save state
* sparse edits
* dirty-region tracking
* irregular / branchy logic
* event-driven updates
* rebuilding derived chunk caches when needed
* simulation scheduling

### GPU Responsibilities

The GPU is a good fit for:

* rendering
* sampling lots of already-prepared data
* lightweight repeated math
* regular, dense compute passes if needed later

The working philosophy is:

> CPU computes or updates authoritative and derived data.
> GPU stores active working-set data and uses it efficiently.

---

## GPU Memory as Memoization Layer

One of the main architecture ideas is to use surplus GPU memory not for waste, but as a memoization / caching layer.

This means using VRAM to store:

* precomputed answers
* derived structure
* summaries
* active field data
* render-ready representations

rather than recomputing the same things repeatedly on CPU or GPU.

This is not just "more cache."
It is an intentional representation strategy.

### Desired outcome

Use memory to reduce:

* repeated CPU recomputation
* repeated GPU inference
* repeated neighbor scans
* repeated surface discovery
* repeated procedural detail evaluation

---

## Important Performance Insight

Some things are easier to compute than to look up.
Some things are easier to look up than to compute.

This is one of the central tradeoffs.

### Prefer compute when:

* math is cheap
* result is used once or rarely
* lookup would be random / incoherent
* storing the result is wasteful
* the value changes constantly

Examples:

* simple hashes
* small transforms
* cheap lighting math
* tiny coordinate logic

### Prefer lookup / cached representation when:

* computation is expensive
* result is reused many times
* access is coherent
* result is spatially local and stable enough
* it avoids scans / traversals / branches

Examples:

* exposed-face masks
* support summaries
* erosion susceptibility
* material tables
* scalar field caches
* chunk summaries
* multi-resolution occupancy structures

Goal:

> no expensive repeated computation without reuse justification
> no trivial computation stored unnecessarily

---

## GPU-Resident Working Set

The GPU should not necessarily hold the entire world.
It should hold the active working set.

Potential working-set categories:

* nearby chunks
* recently visited chunks
* constructed structures
* active simulation zones
* active fields / summaries relevant to rendering and gameplay

A useful residency model may include:

* immediate active zone
* warm / recently visited zone
* cold / CPU-only zone

---

## Rendering Direction

### No dependence on traditional authored textures

The current direction is:

* avoid reliance on conventional hand-authored material textures
* use material tables + procedural shader math where possible

This does not mean banning every possible GPU data texture.
Data textures / lookup tables / scalar-field volumes may still be appropriate.

Better phrasing:

> no dependency on traditional authored material texture pipeline

### Material-driven shading

Per material, possible shader parameters include:

* base color
* roughness
* metallic
* wetness response
* heat response
* crack intensity
* edge wear factor
* erosion response
* emissive tendency

Shader can derive appearance such as:

* slight variation
* cracks
* wetness darkening
* heat tinting
* edge wear
* procedural breakup

without relying on large texture sets.

---

## Geometry Strategy Options

Three rendering directions were identified:

### A. Full cube instancing

Store voxel positions and material IDs, draw cubes via instancing.

Pros:

* simple
* easy prototype
* easy debugging

Cons:

* too much geometry if many hidden cubes

### B. Visible-face instancing / surface-only representation

CPU determines exposed faces and GPU draws only visible surfaces.

Pros:

* strong practical balance
* much less wasted draw cost
* fits CPU-owned world + GPU working set

Cons:

* more preprocessing / bookkeeping

### C. Ray traversal / voxel raycasting

GPU traverses voxel data directly.

Pros:

* elegant
* no explicit face extraction

Cons:

* likely too GPU-cycle-heavy for current assumptions

### Current preferred direction

Surface-oriented representation is currently the most practical.

However, the broader architecture goal is not just "surface-only rendering."
It is to exploit the memory savings from shader-defined materials / cube simplicity to store more useful cached structure.

---

## Derived / Memoized GPU Structures

The most valuable use of spare VRAM is not simply more raw voxel data.
It is derived structure.

Potential GPU-resident derived structures:

### Surface / Visibility

* exposed-face masks
* visible face lists
* boundary cell lists
* surface-only instance buffers

### Neighborhood Summaries

Per voxel / brick / macrocell:

* support counts
* occupancy summaries
* fluid presence
* dominant material
* local gradients
* erosion susceptibility
* thermal hints
* moisture bias

### Hierarchical Structures

* occupancy mip hierarchies
* empty/full flags
* surface-present flags
* min/max field ranges
* low-resolution summaries

### Field Caches

* water
* temperature
* moisture
* stress
* other active scalar fields

### Draw-Ready Buffers

* material buckets
* indirect draw command buffers
* per-LOD surface sets

### Material Table / Shared Properties

Per material:

* shading coefficients
* simulation coefficients
* response curves / factors

These representations should exist only when they actually eliminate repeated expensive work.

---

## Brick / Template / Indirection Layer

An important advanced direction is to exploit not only visual repetition, but structural repetition.

Possible approach:

* use small voxel bricks (e.g. 4^3 or 8^3)
* reuse common brick templates
* reference them via chunk indirection
* allocate unique edited bricks only where changes occur

Benefits:

* structural repetition captured explicitly
* better memory efficiency
* reduced CPU generation / rebuild cost
* good match for repeated geological / architectural motifs

This could become one of the main ways to "cash in" on memory savings.

---

## Storing Iterative Simulation Stage

A major idea that emerged late in the discussion:

Use memory not just to store the current world, but to store the stage / partial progress of iterative simulations.

Goal:

* keep persistent process state
* update additively
* avoid full overwrite
* allow convergence / accumulation over time

Potential patterns:

* accumulation buffers
* delta buffers
* persistent per-chunk field state
* command accumulation + integration
* sparse dirty-region updates

Examples:

* erosion is accumulated gradually until structural change occurs
* crack damage accumulates until collapse becomes possible
* flow preferences accumulate and bias future flow
* heat soak accumulates and alters later material response

This is a major architectural principle.

---

## Command Accumulation + Integration Pattern

Useful pattern for additive simulation:

1. emit local contributions
2. accumulate them into delta-like state
3. integrate into persistent fields periodically
4. trigger discrete changes only when thresholds are crossed

Examples:

* water contributes erosion force
* heat contributes fatigue
* structural load contributes damage
* infrastructure contributes pressure changes

This avoids constant global overwrite and fits slow environmental processes well.

---

## Dirty Regions and Localized Updates

A non-negotiable system for performance is dirty-region tracking.

When something changes:

* mark the affected chunk / brick / tile dirty
* update only the local region
* potentially update immediate neighbors if boundary conditions changed
* upload only changed GPU data

This is one of the core ways to get scalability.

Desired scaling model:

> work should scale with change, not with total world size

---

## CPU -> GPU Update Model

Likely flow:

1. CPU authoritative state changes
2. CPU marks chunk / region dirty
3. CPU recomputes only required derived caches
4. CPU uploads only changed data to GPU
5. GPU samples cached results for rendering / lightweight compute

Important constraints:

* avoid whole-world reuploads
* avoid tiny fragmented upload storms
* keep GPU-side allocations persistent where possible
* batch updates when practical

---

## Performance Expectations

This architecture can produce strong gains if done correctly, but not automatically.

Potential benefits:

* major gains from not doing unnecessary work
* sparse updates instead of full passes
* reduced neighbor rescans
* reduced repeated procedural evaluation
* better scaling for large worlds

Main likely win:

> scalability and stable work growth, not magic FPS from nowhere

Risks:

* too many representations
* invalidation complexity
* upload overhead
* cache thrashing
* storing data with no reuse story

---

## Current Strategic Framing

This project is best framed as:

> a voxel-based, multi-field simulation engine with persistent process memory and GPU-resident memoized world structure

Not:

* merely a voxel renderer
* merely a Minecraft-like clone
* merely a graphics experiment

This is closer to:

* environmental simulation substrate
* engine architecture project
* spatial field lab
* eventual gameplay host

---

## Career Framing

If built correctly, this is a strong project because it demonstrates:

* systems engineering
* data-oriented thinking
* simulation architecture
* CPU/GPU responsibility separation
* performance-aware design
* procedural generation
* memoization / representation engineering

Important: the value comes from architecture and measurable behavior, not from simply saying "voxel game."

---

## Suggested Near-Term Architecture Discussion Topics for Codex

### 1. Core CPU data model

* chunk format
* voxel storage
* sparse edit representation
* field storage strategy
* dirty-region tracking

### 2. GPU working-set model

* what data lives on GPU per active chunk
* buffer vs texture choices
* material table format
* update / upload strategy

### 3. Surface / render representation

* visible-face buffer design
* compact coordinate packing
* per-face / per-voxel metadata
* draw batching / instancing strategy

### 4. Simulation persistence model

* instantaneous vs accumulated fields
* additive updates
* threshold event integration
* sparse field allocation

### 5. Memoization / derived-cache design

* what should be computed vs looked up
* chunk summaries
* neighborhood summaries
* occupancy hierarchy
* field cache structure

### 6. Brick/template system feasibility

* repeated brick storage
* edited brick specialization
* chunk indirection
* implications for streaming and simulation

### 7. First vertical slice

A strong first slice may be:

* chunked voxel world
* material table
* visible-face extraction
* GPU working-set upload
* one persistent field (e.g. moisture or water)
* one accumulated process field (e.g. erosionDebt)
* one threshold event (e.g. remove soil voxel)

---

## One-Line Summary

> Build an evolving voxel simulation engine where the CPU owns the authoritative procedural world, the GPU holds an active memoized working set of derived structure and fields, and environmental processes evolve through incremental additive state rather than constant full recomputation.
