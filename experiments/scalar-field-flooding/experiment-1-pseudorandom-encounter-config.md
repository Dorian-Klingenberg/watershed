# Experiment 1: Pseudorandom Encounter Configuration

## Purpose

Experiment 1 started as a WFC planning space, but the current runnable
prototype is a scalar-field flooding sandbox.

This note remains useful as future-facing procgen planning for how encounter
starting states might eventually be configured once generation is reintroduced.

The central question is:

How should a developer describe an encounter so that each generated instance is
different, but still coherent, solvable, and aligned with the world?

## Design Goal

Each encounter should be pseudorandom rather than fully hand-authored.

That means:

- different seeds should produce different encounter layouts and conditions
- the same seed should reproduce the same encounter
- the result should stay within authored constraints
- the result should preserve recoverability and avoid hidden dead states

## Core Idea

The developer should configure an encounter generator profile rather than a
single fixed map.

That profile defines:

- what kind of encounter this is
- what must exist
- what may vary
- what must never happen

## Generator Inputs

The most useful inputs for a pseudorandom encounter generator are:

- seed
- archetype
- grid size
- anchor requirements
- allowed layer vocabularies
- initial field ranges
- required puzzle affordances
- forbidden states
- validation rules

## Recommended Authoring Model

Use a hybrid model:

1. Archetype defines high-level identity
2. Anchors define non-negotiable structural elements
3. Randomized weights define variation
4. Field ranges define starting pressures and instability
5. Validation ensures the rolled encounter is acceptable

This keeps the encounter varied without becoming arbitrary.

## What Should Be Guaranteed

Some things should not be left fully to chance.

Each generated encounter should usually guarantee:

- a recognizable infrastructure problem
- at least one reachable intervention point
- at least two meaningful intervention options
- at least one clue or inspectable signal
- at least one recovery path from a bad move
- no hidden unsolvable non-failure state

These guarantees are part of the encounter identity, not optional polish.

## What Should Vary By Seed

The following are good candidates for pseudorandom variation:

- exact canal break placement
- path layout
- modern workaround placement
- clue location
- salvage or tool position
- local terrain wetness pockets
- initial stress distribution
- optional shortcut opportunities
- degree of downstream fragility

This gives each seed texture while preserving the same overall problem shape.

## Archetype-Driven Generation

The best long-term model is likely archetype-driven generation.

An archetype describes a class of encounter such as:

- flooded inspection route
- cracked reservoir lip
- village bypass theft
- marsh expansion around broken canal
- pressure choke near buried regulator

Each archetype should define:

- intended player fantasy
- likely failure mode
- mandatory anchors
- preferred layers
- typical field ranges
- likely consequence pattern

## Example Encounter Spec Shape

Conceptually, an encounter specification may look like this:

```text
Archetype: Flooded Inspection Route
Seed: 18427
GridSize: 16x16

RequiredAnchors:
- water_source
- settlement_route
- damaged_canal_segment
- control_point

AllowedVariation:
- canal crack position
- saturation pockets
- salvage placement
- bypass layout
- clue position

FieldRanges:
- pressure: medium-high to high
- saturation: medium to high near the breach
- stress: elevated on old containment structures

Guarantees:
- at least 2 interventions
- at least 1 risky shortcut
- at least 1 recoverable bad state
- no hidden dead state
```

## Best Questions For This Experiment

Experiment 1 should help answer:

- What does a developer actually author by hand?
- What is generated from that input?
- Which guarantees are essential?
- Which variations are interesting rather than noisy?
- Which encounter specifications are easiest to reason about?
- Which configuration style best supports validation?

## Candidate Configuration Styles To Compare

### 1. Constraint-First

The developer writes hard requirements and lets the generator fill the rest.

Good for:

- strong solvability control
- scalable procedural generation

Risk:

- may feel abstract or hard to author at first

### 2. Archetype-First

The developer chooses a named encounter family, then adjusts weights and
constraints.

Good for:

- content velocity
- thematic consistency

Risk:

- can become repetitive if archetypes are too rigid

### 3. History-First

The developer begins with a small causal story about what happened here.

Examples:

- villagers added a bypass to survive a dry season
- a cracked wall now leaks under pressure
- a spirit-assisted patch stabilized the site but increased hidden stress

Good for:

- world coherence
- consequence-rich scenarios

Risk:

- requires a translation layer into actual generator constraints

### 4. Field-First

The developer configures pressure, saturation, stress, and similar live values
before deriving the most likely layered tile arrangement.

Good for:

- simulation-led design
- strong consequence modeling

Risk:

- less intuitive as a starting point for authors

## Likely Best Long-Term Direction

The strongest long-term system is probably a hybrid:

- archetype-first for identity
- constraint-first for solvability
- history-first for meaning
- field-first for dynamic tension

The developer should be able to describe:

- what kind of problem this is
- why it exists
- what must be present
- what can vary by seed
- what safety guarantees must hold

## Validation Requirements

Because encounters are pseudorandom, validation is critical.

Each rolled encounter should be checked for:

- structural coherence
- required anchors present
- field values inside acceptable bounds
- at least one success path
- at least one recovery path
- no hidden dead states
- sufficient clueing for player reasoning

Bad rolls should be rejected automatically.

## Suggested First Archetypes To Brainstorm

These are good candidates for initial seed experiments:

- canal choke point
- flooded inspection path
- village flow theft
- false repair site
- buried regulator access problem

Each should be tested with several seeds to see:

- how much variation appears
- whether the variation stays legible
- whether the encounter remains recoverable
- whether different seeds suggest different player hypotheses

## Practical Outcome For Experiment 1

By the end of this experiment, we should ideally have:

- a shortlist of configuration styles
- 3 to 5 archetypes worth prototyping
- a first draft of an encounter spec format
- a list of guarantees that every seed must satisfy
- a list of variables that are safe and interesting to randomize

## Summary

Experiment 1 is about defining the authoring language for pseudorandom
encounters.

The target is not "random maps."
The target is reproducible, varied, recoverable systemic encounters generated
inside a strong design envelope.
