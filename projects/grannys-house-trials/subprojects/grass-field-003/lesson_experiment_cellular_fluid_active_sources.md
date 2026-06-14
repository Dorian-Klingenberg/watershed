# Experiment Lesson: Optimized Round 2 - Active Sources

---

## Chapter 1: Why Circle Back To The CPU

Round 1 optimized costs that could be removed without changing which cells
participate in a tick:

```text
reuse the delta buffer
fast-path interior topology
cache drainage indices
reuse diagnostic storage
```

The CPU Parallel and HLSL branches then explored executing similar work on more
processors. A deferred CPU idea remained untested:

```text
Do not compute outgoing flow for cells that contain no transferable water.
```

This is an important experiment before memory tiling. Tiling makes work cheaper
once it is performed. Active sources asks whether much of that work can be
skipped entirely.

---

## Chapter 2: The Full-Field Cost

The Trial 3 water grid contains:

```text
1200 x 1200 = 1,440,000 cells
```

Round 1 examines every source cell every tick, even for a small center pour.
Most dry cells return quickly, but each one must still be visited and tested.

```mermaid
flowchart LR
    Grid["Full grid<br/>1,440,000 source checks"] --> Dry["Dry cells<br/>early return"]
    Grid --> Wet["Wet cells<br/>evaluate neighbor flow"]
    Wet --> Delta["Accumulate deltas"]
    Dry --> Cost["Still consumes traversal cost"]
```

The optimization hypothesis is workload-dependent:

| Workload | Prediction |
|---|---|
| Center pour | Active sources should help because wet cells occupy a small region |
| Uniform rain | Active sources may not help because most or all cells are wet |

---

## Chapter 3: What Counts As Active

The baseline already has this early return:

```cpp
if (available_water <= minimum_water_inches_)
    return;
```

Round 2 defines an active source using that exact condition:

```text
active source = water depth > minimum_water_inches
```

It does not try to predict whether a wet cell will actually find a lower
neighbor. That would add a more complex eligibility rule. The first experiment
only removes work already proven to be an immediate no-op in the baseline.

```mermaid
flowchart TD
    Cell["Source cell"] --> Test{"water > minimum?"}
    Test -->|"No"| Skip["Baseline would return<br/>skip in Round 2"]
    Test -->|"Yes"| Evaluate["Run identical<br/>neighbor flow equation"]
```

---

## Chapter 4: Preserving The Tick

The selectable variant is:

```text
Cellular Water Flow (Optimized Round 2 - Active Sources)
```

It preserves:

- The same fractional water state.
- The same surface-equalization equation.
- The same maximum flow and settle-rate limits.
- The same four neighbors in `left, right, up, down` order.
- The same synchronous delta application.
- The same boundary drainage.

It changes one traversal:

```text
Round 1: evaluate every source in row-major order
Round 2: evaluate only wet sources, stored in row-major order
```

Dry sources produced no delta additions in Round 1. Omitting them therefore
leaves the order of every contributing floating-point addition unchanged.

## Sequence Interaction Diagram

```mermaid
sequenceDiagram
    participant R1 as Round 1 Full Scan
    participant R2 as Round 2 Active Scan
    participant D as Delta Buffer
    R1->>R1: Visit dry source 0, no write
    R1->>D: Wet source 24 writes transfer
    R1->>R1: Visit dry source 25, no write
    R1->>D: Wet source 30 writes transfer
    R2->>D: Wet source 24 writes same transfer
    R2->>D: Wet source 30 writes same transfer
    Note over D: Contributing write order is unchanged
```

---

## Chapter 5: Round 2A Scope

This first active-source version intentionally does not make all data sparse.
Each tick is:

```mermaid
flowchart TD
    A["Clear full delta array"] --> B["Evaluate active sources only<br/>ordered row-major"]
    B --> C["Apply delta to full water array"]
    C --> D["Drain cached edges"]
    D --> E["Scan water depth<br/>rebuild ordered active list"]
    E --> A
```

Why keep full-array clearing and application?

- It gives the smallest meaningful behavior change.
- It makes exact comparison against the baseline straightforward.
- It measures whether avoiding empty flow calculations is worthwhile before
  adding sparse-update bookkeeping.

This version still performs a cheap full-field water threshold scan to build
the next list. For localized water, that scan replaces a full field of
neighbor-flow evaluation. For broad rain, the list provides little avoidance
and adds management overhead.

---

## Chapter 6: Files Changed

| File | Purpose |
|---|---|
| `sim/simple_cellular_fluid_sim_active_sources.h` | New deterministic active-source simulator |
| `main.cpp` | Adds Round 2 as a selectable simulator while retaining Round 1 as default |
| `fluid_sim_benchmark.cpp` | Times and exactly compares Round 2 alongside existing CPU variants |

The preserved experiment ladder is now:

```mermaid
flowchart LR
    B["Baseline"] --> R1["Optimized Round 1<br/>full scan cleanup"]
    R1 --> R2["Optimized Round 2<br/>active sources"]
    R1 --> P["CPU Parallel<br/>proposal experiment"]
    B --> G["HLSL Compute Phase 1<br/>GPU experiment"]
```

---

## Chapter 7: Correctness Test

The existing console benchmark now initializes identical terrain and water for
the baseline and Round 2, advances both simulations through 30 ticks, and
compares every cell bit-for-bit after each tick.

```mermaid
flowchart LR
    Seed["Same seed and water input"] --> Base["Baseline<br/>step 1 through 30"]
    Seed --> Active["Round 2 Active Sources<br/>step 1 through 30"]
    Base --> Compare["Bitwise water comparison<br/>every cell, every tick"]
    Active --> Compare
    Compare --> Result["PASS required<br/>before adoption"]
```

The acceptance rule remains strict:

```text
faster + exact = viable optimization
faster + not exact = separate simulation variant, not an optimization
```

---

## Chapter 8: Timing Protocol

The benchmark reports:

```text
5 warmup steps
30 measured steps
3 repetitions
median milliseconds per step
```

Scenarios:

| Scenario | Reason |
|---|---|
| Center pour: radius `11`, depth `22 in` | Tests sparse localized water |
| Uniform rain: depth `1 in` | Tests dense water where active lists may not help |

Release benchmark results recorded on May 27, 2026:

| Scenario | Baseline ms/step | Round 1 ms/step | Round 2 Active ms/step | Round 2 speedup | Exact state |
|---|---:|---:|---:|---:|---|
| Center pour: radius `11`, depth `22 in` | 5.562 | 4.341 | 3.145 | 1.768x | PASS |
| Uniform rain: depth `1 in` | 22.438 | 17.518 | 29.168 | 0.769x | PASS |

Environment during this run:

```text
Build: Release
Grid: 1200 x 1200 cells
Hardware threads reported by C++ runtime: 16
```

Round 2 is numerically validated for the tested scenarios and first 30 steps.
Its performance hypothesis is also confirmed: avoiding dry-source flow work is
valuable for localized water, while maintaining the active list is a loss when
the full field is wet.

---

## Chapter 9: Possible Follow-On Rounds

Round 2A answers whether active source evaluation alone is useful. It does not
claim to be the final sparse CPU representation.

Round 2A is exact in this benchmark and helps localized water. One possible
later experiment can track only cells whose deltas may change:

```mermaid
flowchart LR
    Sources["Active sources"] --> Touched["Touched set<br/>sources + receivers"]
    Touched --> Apply["Sparse delta clear/apply"]
    Apply --> Near["Activate nearby candidates<br/>for next tick"]
```

That step has higher proof risk because changing one cell's water can alter a
neighbor's ability to flow on the next tick. It should be a separate variant,
not silently folded into this one.

The dense rain case is slower, so the immediate next experiment should be a
deterministic hybrid that chooses:

```mermaid
flowchart TD
    Count["Active source count"] --> Dense{"Large active fraction?"}
    Dense -->|"No"| Sparse["Round 2 active-source path"]
    Dense -->|"Yes"| Full["Round 1 full-scan path"]
    Sparse --> State["Same next state"]
    Full --> State
```

This is also separate future work. Round 2A first gives us a simple, testable
measurement of avoided dry-source calculations.

---

## Chapter 10: Decision From The Measurement

Round 2 should remain available as the proven sparse-water experiment, but it
should not replace Round 1 as the general default yet:

```mermaid
flowchart TD
    Result["Measured Round 2"] --> Sparse["Localized pour<br/>1.768x baseline, PASS"]
    Result --> Dense["Uniform rain<br/>0.769x baseline, PASS"]
    Sparse --> Keep["Keep active-source path"]
    Dense --> Hybrid["Add deterministic density switch"]
    Keep --> Hybrid
    Hybrid --> Goal["Use sparse work only when it actually avoids enough work"]
```

The next CPU optimization round is therefore:

```text
Optimized Round 3 - Hybrid Active/Full Scan
```

It should reuse Round 2 when the active fraction is low and fall back to the
existing exact Round 1 traversal when the active fraction is high. Each branch
already preserves baseline behavior; the experiment will measure whether a
simple deterministic selection rule captures both advantages.
