# Agent Launchers

Use this repository-local bootstrap and keep context narrow.

## Codex Desktop

- Start from [bootstrap.json](./bootstrap.json)
- Load [../projects/grannys-house-trials/AGENT_CONTEXT.json](../projects/grannys-house-trials/AGENT_CONTEXT.json) first
- Then load the maintained human docs only as needed

## Codex CLI In WSL

- Start from [bootstrap.json](./bootstrap.json)
- Prefer WSL/bash-native commands for inspection, build, and test work
- Avoid switching to Windows shells unless a Windows-native tool is required

## Copilot / WSL CLI Agents

- Start from [bootstrap.json](./bootstrap.json)
- Use the compact agent context file as the primary repo summary
- Expand into prose docs only when the task needs more detail

## Shell Preference

- Prefer WSL/bash-equivalent shells over Windows shells for reliability in
  this repo's current tooling mix
- Use Windows shells only for Windows-specific UI, debugger, or toolchain
  tasks that need them. This is a preference, not a general criticism of
  Windows.
