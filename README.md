# personal-assistant-ui

Document-centric second brain and agent orchestration repo.

## Core Idea

- Durable knowledge lives in plain Markdown.
- AI and agents operate over Markdown; they do not own it.
- Copilot is for speech, language, and explanation.
- The backend is agnostic of Copilot, Microsoft Office, and UI concerns.
- UI surfaces are instruments for capture, triage, and explicit promotion.

## Repo Guidance

- [`copilot-brain.md`](./copilot-brain.md) is the primary instruction set for Copilot.
- [`codex-brain.md`](./codex-brain.md) is the companion instruction set for Codex-style agents.

## Guiding Question

Could this logic work identically if the client were CLI and the model were local?

If not, it belongs in the wrong layer.
