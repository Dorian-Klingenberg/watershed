# Fluidic Logic

## Purpose

This document defines how ancient infrastructure performs logic-like behavior without electronics or moving machinery.

The core idea is:

- pressure acts as signal
- flow acts as execution
- basins act as memory
- siphons act as stateful release paths
- geometry acts as control law

## Fluidic AND

The preferred ancient AND pattern is:

1. enough inland head exists to reach a siphon trigger lip
2. downstream backpressure is low enough for the siphon to sustain

Only when both conditions are true does the release path fully activate.

## Fluidic XOR

A useful XOR pattern comes from competing siphons or mutually disruptive paths.

Examples:

- two siphon throats share the same priming basin, but only one can retain prime
- one path introduces air into the other when its branch dominates
- asymmetric geometry favors whichever side gains head first

## Threshold Behavior

Thresholds emerge from:

- lip height
- chamber volume
- conduit cross-section
- trapped air amount
- sediment-reduced capacity

Thresholds should drift rather than behave like immutable binary rules.

## Oscillation

Fluidic oscillation can emerge when:

- a basin fills faster than it drains
- a siphon repeatedly primes and breaks
- an air chamber alternates between compression and venting
- a spillway relieves pressure that later rebuilds

## Failure Modes

Ancient fluidic systems should usually fail by miscomputing.

### Sediment Accumulation

- increases resistance
- delays siphon priming
- distorts sensed backpressure

### Leaks

- reduce effective head
- create false wetlands
- weaken intended delivery

### Air Disruption

- breaks siphons
- prevents stable priming
- creates intermittent release

### Geometry Drift

- shifts thresholds
- changes branch priority
- turns stable regulation into subtly wrong regulation

## Design Rule

Do not model ancient logic as `open/closed gate`, `valveState`, or actuator toggles.
Model it as pressure differential, siphon state transition, basin charge, air volume, and geometry-biased flow.
