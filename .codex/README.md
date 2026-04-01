# Codex Bootstrap

This directory holds the small machine-readable bootstrap used to keep Codex
context narrow and fast for this repository.

Primary entrypoint:

- [bootstrap.json](./bootstrap.json)

The bootstrap should stay compact and should point at maintained human docs
plus the canonical agent context file in `projects/grannys-house-trials`.

Launcher guidance:

- Codex Desktop should start from `.codex/bootstrap.json`.
- Codex CLI in WSL should do the same and prefer WSL/bash-native commands for
  repo work.
- Copilot or related WSL CLI agents should also start from the same bootstrap
  and treat the compact agent context as the first load target.
- Prefer WSL/bash-equivalent shells over Windows shells when doing build,
  test, or repository inspection work on this machine unless the task requires
  Windows-native behavior. This is just a practical preference for this
  repo/tooling mix and current machine state, not a general claim about
  Windows or the machine itself.
