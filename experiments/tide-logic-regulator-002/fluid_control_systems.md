# Fluid Control Systems & Hydraulic Logic

## Core Idea

Ancient systems can perform control and computation using:

- water flow
- pressure
- geometry
- time

These are effectively:
> analog computers made of water

---

## Fundamental Mechanisms

### Threshold Gate
- opens when pressure exceeds limit

### Pilot-Controlled Gate
- small pressure controls large flow
- hydraulic "transistor"

### AND Logic
- two conditions must be satisfied
- e.g. reservoir high AND tide low

### NOT / Inversion
- pressure blocks or redirects flow

### Memory Cistern
- stores water → retains state
- prevents oscillation

### Delay Basin
- slows signal propagation
- introduces timing

### Oscillation Chamber
- periodic fill/drain cycles

### Overflow Path
- sacrificial safety system

---

## System Behavior Types

- passive (terrain, gravity)
- reactive (thresholds)
- stateful (memory)
- distributed (network-level logic)

---

## Failure Modes

### Sediment
- blocks flow
- delays signals

### Leakage
- reduces pressure
- creates new paths

### Structural Drift
- changes thresholds
- causes misbehavior

### Collapse
- reroutes flow

### Human Modification
- local fix → global instability

---

## Key Insight

Systems do not fail cleanly.

They:
- miscompute
- produce incorrect outputs
- behave unpredictably

---

## Tide-Logic Regulator (Reference System)

Function:
- release water only when:
  - reservoir pressure is high
  - tide pressure is low

Components:
- reservoir input
- tide input
- logic chamber (AND gate)
- memory cistern
- main gate
- overflow path

---

## Behavior

Correct:
- stable release cycles
- flood prevention

Degraded:
- false signals
- gate chatter
- incorrect release timing
- downstream flooding

---

## Design Principle

> The system still works — just incorrectly

---

## Core Identity

> Water is not just transported — it is used to compute decisions