# LLM-Readable Puzzle Reports

Every generated or hand-authored puzzle should be exportable as a Markdown report another LLM can read.

## Report structure

```markdown
# Puzzle Report: GHT_XXXX

## Summary

One paragraph describing the puzzle.

## Map Features

- Cistern: location and behavior
- Canal: route and reliability
- Terraces: number and overflow sequence
- House: location and basement hazard
- Boulder wall: location and stability concern
- New garden: target and constraints

## Goals

List all primary, safety, quality, optional, and failure goals.

## Known Happy Path

Step-by-step action sequence.

## Designed Traps

Describe each tempting bad solution and expected failure.

## Final Metrics

Table of metrics after happy path and trap scripts.

## Event Log Highlights

Important events.

## ASCII Maps

Initial map and final map.

## Notes for Designers

What this puzzle teaches and what to tune.
```

## Why this matters

The user wants to collaborate with coding agents and LLMs. If every puzzle can explain itself, then agents can inspect generated content, suggest fixes, and compare design intent to simulation behavior.
