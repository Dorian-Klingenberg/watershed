# Granny's House Trials History And Grounding Index

## Why This File Exists

This is a compact orientation index for future agents.

Use it when you need:

- the rough history
- the current milestone state
- the first files to read
- the known places where docs and code drifted apart

## Rough Project Arc

The project broadly moved through these stages:

1. define the slice as a world-first testing-as-show experiment
2. build a small D3D12 terrain-and-camera harness
3. choose a scoped renderer path instead of chasing general traversal
4. add a real deterministic drainage scenario
5. extract the round/session boundary into shared playtest code
6. add evidence-board and end-round summary behavior
7. begin cast/personality and agent-memory framing
8. prepare the work for a broader systems-engineering context

## Milestone State

Current practical state:

- M1 complete
- M2 complete
- M3 complete
- M4 complete
- M5 partial
- M6 not started in a meaningful way
- M7 not started in a meaningful way

## Important Commits

Useful waypoints:

- `8ae773d`
  - large early setup/update burst
- `87375d5`
  - renderer workflow and build-time shader improvements
- `3a33043`
  - shader reorganization
- `57b5c75`
  - bootstrap/context recentering
- `a4d56a8`
  - milestone 4 work
- `332e870`
  - milestone 4 completion
- `5e20239`
  - milestone 5 cast layer / agent personality files
- `5492e03`
  - start work on new scenario
- `b88347a`
  - new workspace context plus renderer-module additions

## Read These First

Primary project docs:

- `README.md`
- `PROJECT_BRIEF.md`
- `STATUS.md`
- `MILESTONES.md`
- `PLAYTEST_PROTOCOL_V0.md`
- `SCENARIO_001_GRANNYS_YARD.md`
- `CAST_AND_SCORING.md`
- `DEVELOPMENT_GUIDE.md`

Primary code entry points:

- `modules/sim/src/grannys_yard_scenario.cpp`
- `modules/playtest/src/grannys_yard_session.cpp`
- `modules/playtest/src/turn_packet.cpp`
- `subprojects/grass-field-001/main.cpp`
- `subprojects/grass-field-001/README.md`

Primary tests:

- `tests/playtest/scenario_001_script_regression_tests.cpp`
- `tests/playtest/grannys_yard_session_tests.cpp`
- `tests/playtest/round_result_tests.cpp`
- `tests/playtest/evidence_board_view_tests.cpp`
- `tests/playtest/playtest_protocol_tests.cpp`

Renderer support docs/code:

- `modules/gfx/d3d12_renderer/ARCHITECTURE.md`
- `modules/gfx/d3d12_renderer/README.md`
- `modules/gfx/d3d12_renderer/PHASE_3_COMPLETION.md`
- `tests/d3d12_renderer_test.cpp`
- `tests/gfx/ui_frame_renderer_tests.cpp`

## Known Drift To Watch For

### Milestone guidance drift

Some docs still point toward Milestone 4 as the next focus even though
Milestone 4 is effectively complete.

### Renderer-status drift

Some renderer docs still describe the D3D12 module as architecture-only future
work even though the code and build graph show live implementation.

### Runnable-count drift

Some docs still speak as if there is only one active runnable target, even
though `grass-field-002` exists as a real target now.

### Test-wiring gap

`tests/d3d12_renderer_test.cpp` exists, but as of the latest review it was not
yet wired into the test target, so green test output overstated D3D12 coverage.

## Current Strategic Takeaway

The most useful thing to inherit is not the exact Granny fiction.

It is the structure:

- system truth in `sim`
- operator/tester projection in `playtest`
- thin host shell
- factual evidence capture
- human judgment layered over deterministic facts
