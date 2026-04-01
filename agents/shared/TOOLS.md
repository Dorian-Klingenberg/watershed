# Shared Tools

Skills and action vocabulary shared by all testing agents.

## Action Interface

All agents share the same decision interface for reproducible evaluation.

```cpp
struct Action
{
    enum Type {
        OpenGate,
        CloseGate,
        RemoveVoxel,
        PlaceVoxel,
        MoveTo,
        Wait
    };

    int x, y, z;
};

class Agent
{
public:
    virtual Action Decide(const WorldState& state) = 0;
    virtual const char* GetName() const = 0;
};
```

**Note:** This interface is a design suggestion, not finalized implementation.

## Action Vocabulary

| Action | What it does |
|---|---|
| `OpenGate` | Open a sluice gate or valve at (x, y, z) |
| `CloseGate` | Close a sluice gate or valve at (x, y, z) |
| `RemoveVoxel` | Remove terrain or structure at (x, y, z) |
| `PlaceVoxel` | Place material at (x, y, z) |
| `MoveTo` | Move to position (x, y, z) |
| `Wait` | Do nothing this tick |

## Named Anchors (Granny's Yard)

First-pass stable locations agents can observe or act from:

- `porch`
- `path_edge`
- `terrace_cut`
- `drain_mouth`
- `cellar_lip`
- `garden_bed_north`

## Trio Dynamics

Running the three agents together gives a clear entertainment + debugging loop:

- **Jeremy** causes the problem with overconfident, large-scale moves.
- **Richard** amps the chaos by reacting impulsively and trying to patch what he can see.
- **James** observes, explains, and attempts a minimal correction while the others keep upsetting the balance.

This loop keeps the world legible while giving the tester a narrative frame for
replaying the interaction.

## Operating Constraints

These agents must remain:

- **Deterministic**: no hidden randomness beyond what is already in the simulation
- **Explainable**: action traces deliver a short story you can narrate
- **Replayable**: the same setup should behave the same way each run

## File Variants

Each agent workspace has two tiers of memory files:

| File | Target | Purpose |
|---|---|---|
| `MEMORY.md` | LLM | Rich narrative. Full context, multi-paragraph entries. Used for testing, tuning, and human readability. |
| `MEMORY.slm.md` | SLM | Dense behavioral triggers. Tagged patterns, short entries, voice markers. Optimized for small model token budgets. |

Convention: IDENTITY.md, SOUL.md, and AGENTS.md are shared across both tiers.
Only MEMORY files are split. The SLM variant compresses the same behavioral
content into pattern-matchable entries the deployment model can act on directly.

When updating agent memories, update the LLM file first (it is the source of
truth), then compress changes into the SLM file.
