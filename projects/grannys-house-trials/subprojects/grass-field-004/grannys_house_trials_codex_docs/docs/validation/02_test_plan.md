# Test Plan

## Unit tests

### Terrain and materials

- Cell surface height equals elevation + water.
- Material rules load correctly.
- Diggable materials can be modified.
- Non-diggable materials reject dig actions.
- Absorption changes saturation and reduces water.

### Water stepping

- Water flows from higher surface to lower surface.
- Water does not flow uphill.
- Canal carries more water than grass/soil.
- Closed cistern releases no water.
- Low/medium/high gate levels release expected water per tick.

### Actions

- `clear_weeds` reduces resistance or removes blockage feature.
- `dig` lowers elevation within bounds.
- `place_stone` adds roughness/check-dam effect.
- `repair` changes damaged feature to functional feature.
- `open_gate` sets source flow level.
- `wait` advances simulation without modification.

### Goals

- Old garden hydration metric increases when terrace beds saturate.
- New garden hydration metric increases when lower garden saturates.
- Basement dampness increases near north/west seepage zones.
- Boulder stability decreases under high flow.
- Erosion damage increases under fast flow over weak material.

## Scenario tests

### Static happy path test

Load `examples/ght_static_001.json` and run `examples/happy_path_001.json`.

Expected:

- Success.
- Old garden watered.
- New garden watered.
- Basement dry enough.
- Boulder wall stable.

### Trap: dig straight downhill

Expected:

- Water reaches lower area quickly.
- Basement dampness rises.
- May trigger boulder wall instability.
- Should fail safety goal.

### Trap: open full cistern too early

Expected:

- Higher flow.
- Terrace may overtop.
- Erosion or basement risk increases.
- Failure should be understandable.

### Trap: use hidden drain

Expected:

- Surface water seems reduced.
- Basement dampness increases from hidden conduit.
- Event log includes `HIDDEN_DRAIN_ACTIVATED`.

## Generated puzzle tests

For each generated puzzle seed:

1. Generate puzzle.
2. Validate happy path.
3. Validate at least one trap script.
4. Ensure no automatic success before actions.
5. Dump metrics and ASCII map.

## Regression policy

Any accepted seed should be saved in a regression list.

If rule changes cause a previously accepted seed to fail, either:

- update the seed metadata and expected metrics, or
- fix the rule change if it was accidental.

## Debug artifacts

Each scenario run should output:

- final metrics JSON
- event log JSONL
- initial ASCII map
- final ASCII map
- optionally per-tick water snapshots

## Example command targets

```bash
# run static scenario
python -m ght run examples/ght_static_001.json --script examples/happy_path_001.json

# run trap
python -m ght run examples/ght_static_001.json --script examples/trap_straight_downhill_001.json

# generate and validate 100 seeds
python -m ght generate --count 100 --validate --out generated/
```
