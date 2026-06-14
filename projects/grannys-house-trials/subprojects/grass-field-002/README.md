# Agent Sandbox

Modern UI harness for observing agent behavior in Scenario 002 (Preserve Stability).

## Purpose

Replace grass-field-001 with a cleaner observation-focused interface using ImGui for:

- World rendering (D3D12 voxel visualization)
- Interactive voxel inspection (right-click select, view state)
- Agent location and reasoning panels (per-agent internal state)
- Conversation log (agent-to-agent communication + actions)
- World state queries (pause, replay, constraint inspection)

Important renderer boundary:

- 002 is a host/UI harness over the same world renderer used by 001.
- The world render path, world shaders, and scene geometry logic are shared,
	not reimplemented locally in 002.
- 002-specific work should stay in panels, controls, and session interaction
	glue.

## Architecture

- **Rendering:** D3D12 + DirectXMath (retained from grass-field-001)
- **UI:** ImGui overlay (immediate-mode, zero external process overhead)
- **Simulation:** GrannysYardSession + Scenario 002
- **Agent Integration:** Structured action packets with reasoning annotations

Parity rule:

- Any world-render change must be validated against 001 in the same scenario
	state before merge.

## Development Phases

### Phase 1: Voxel Inspection (Current)
- [ ] World render loop (terrain + moisture visualization)
- [ ] Mouse ray-casting to voxel selection
- [ ] Voxel inspector panel (elevation, moisture, ground type, soaking state)
- [ ] Right-click context menu for actions

### Phase 2: Agent Panels
- [ ] Agent location markers on world
- [ ] Per-agent reasoning display (internal state + decision logic)
- [ ] Action history panel

### Phase 3: Conversation Log
- [ ] Agent-to-agent message log
- [ ] Action-consequence linking
- [ ] World state snapshots at key events

## Building

Requires ImGui to be downloaded first. Run:
```powershell
./scripts/setup-dependencies.ps1
```

Then build normally:
```
cmake --build build/vs2026-debug --config Debug --target grannys_house_trials_grass_field_002
```

## Running

```
./build/vs2026-debug/projects/grannys-house-trials/subprojects/agent-sandbox/Debug/grannys_house_trials_grass_field_002.exe
```

## Next Steps

Start with terrain rendering + voxel selection to validate D3D12 integration and ray-casting accuracy.
