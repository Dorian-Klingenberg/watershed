# GPT Creative Context

This document contains the creative context for the project.

When pasted into a ChatGPT conversation it should be treated as the
authoritative description of the project unless explicitly overridden.

Purpose:
- restore project context quickly
- maintain design continuity across chats
- support creative exploration

ChatGPT should treat this file as background context and help expand
ideas, analyze systems, and propose mechanics consistent with it.

This file is not authoritative for repository changes.
Repository changes must be introduced through the transaction system.

Codex and automated agents should ignore this file.

# Context

# Project Context Summary

## Project Goal

Develop a **systems-first simulation game** centered on exploring and interacting with the decaying infrastructure of an ancient civilization.

The project is also a **portfolio and career-alignment project** focused on:

- simulation systems
- graphics experimentation
- world-driven gameplay
- complex system modeling
- tool-building and AI-assisted development workflows

The project serves both as a game prototype and as a **laboratory for simulation ideas**.

---

## Core Game Concept

The game world exists within the remnants of a highly advanced civilization that existed roughly **10,000 years ago**.

This civilization constructed massive environmental and transportation systems including:

- aqueduct networks
- water transportation routes
- climate manipulation structures
- large-scale environmental control infrastructure

These systems still partially function but their design is **no longer understood** by present-day inhabitants.

As a result, the world experiences:

- swamps forming where canals leak
- floods from uncontrolled flow
- deserts forming where systems fail
- unstable ecosystems
- settlements dependent on fragile infrastructure

The world effectively exists inside a **degrading megasystem**.

---

## Core Gameplay Idea

Players interact with the world by **investigating and repairing infrastructure systems**.

Typical player activities:

- exploring ancient structures
- diagnosing failing systems
- improvising repairs
- modifying flow networks
- responding to environmental consequences

Repairs are **not guaranteed to be safe**.

Key mechanic:

Local fixes may produce **distant or delayed consequences**.

Examples:

- repairing a canal increases downstream flooding
- sealing a leak dries out a wetland ecosystem
- redirecting water destabilizes other settlements

Understanding the system becomes the core gameplay progression.

---

## Simulation Model

The world simulation uses a **layered tile system**.

Each tile can contain multiple overlapping layers whose properties combine to produce emergent behavior.

Proposed layers include:

- terrain
- ancient infrastructure
- modern modifications
- water flow
- structural integrity
- ecology
- arcane influence

Tile behavior results from **combined physical and systemic properties**.

Systems may propagate effects across large distances.

---

## Ancient vs Modern Infrastructure

Ancient infrastructure is:

- elegant
- interconnected
- large-scale
- durable but degrading
- poorly understood

Modern inhabitants often build **improvised modifications** such as:

- temporary dams
- patched canals
- diversion trenches
- mechanical valves
- small repair systems

These modifications may stabilize or destabilize ancient systems.

---

## Magic System Concept

Magic exists as two broad paths.

### Mastery Path

Power gained through:

- study
- meditation
- discipline
- understanding the underlying nature of reality

This path is slow but stable.

At extreme mastery levels practitioners may become detached from normal worldly concerns.

### Spirit Path

Power gained by invoking **external entities or spirits**.

Characteristics:

- fast access to power
- minimal training required
- dependency on external intelligences
- unclear alignment of interests

Spirits are not necessarily evil but may manipulate or exploit those who call them.

Overreliance can corrupt or transform the practitioner.

---

## Design Philosophy

The game prioritizes:

- world-first design
- systemic interactions
- discovery through experimentation
- unintended consequences
- environmental storytelling
- partial understanding
- emergent gameplay

Avoid:

- overly scripted narratives
- simplistic morality
- shallow fantasy tropes
- lore-heavy exposition

Systems should produce **interesting outcomes through interaction**.

---

## Development Workflow

Creative design exploration occurs primarily in **ChatGPT discussions**.

Implementation and documentation occur in a **Git-based repository**.

A **transaction-based workflow** is used to move ideas from discussion into the repository.

Workflow:

idea → transaction → repository update → transaction history

---

## Transaction System

Transactions describe structured changes to the repository.

Pending work is stored in:

docs/transactions.md

Completed transactions are moved to:

docs/transaction_history.md

Each transaction includes:

- description
- target files
- changes to apply
- status
- timestamps
- executor

This system allows AI agents or developers to safely apply changes.

---

## Agent Roles

Agents interacting with the project should default to:

Advisor Mode
Creative Partner Mode

Responsibilities include:

- exploring system design ideas
- proposing mechanics
- analyzing consequences
- helping structure development

Additional modes include:

Planner Mode — break work into steps  
Builder Mode — implement approved systems

Agents should avoid premature coding unless requested.

---

## Project Focus Areas

Key areas of exploration include:

- layered tile simulation
- hydrology systems
- infrastructure failure mechanics
- cascading environmental effects
- ancient vs modern system interaction
- repair gameplay mechanics
- professions related to infrastructure maintenance
- world exploration and discovery

The game should feel like **investigating and interacting with a living system**.

---

## Development Tools

The project uses:

- C++
- DirectX
- simulation experimentation
- AI-assisted design
- Codex for implementation support
- ChatGPT for creative exploration and system design

The goal is to build **interactive experiments and simulations** that inform gameplay.

---

## Long-Term Vision

The project functions as both:

- a playable game concept
- a research environment for simulation systems

It should produce:

- interesting emergent gameplay
- technical simulation experiments
- portfolio-quality systems work
- a framework for exploring complex world simulations