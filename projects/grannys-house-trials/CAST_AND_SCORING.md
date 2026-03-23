# Cast And Scoring

## Purpose

This document defines the first-pass cast structure and the competition format
for `Granny's House Trials`.

The key rule is:

> the system tracks evidence, but the host awards the actual points

That separation should remain intact unless there is a very strong reason to
change it later.

## Core Cast

### The Builder

Role:

- practical
- optimistic
- wants the system to genuinely work
- attached to intended behavior

What they are good at:

- completing the stated task
- making reasonable interventions
- showing what the "clean" path was supposed to be

What makes them funny:

- trusting the system too early
- explaining while standing in the hazard zone
- being betrayed by implementation reality

### The Chaos Tester

Role:

- aggressive stress-tester
- spectacle generator
- edge-case finder
- reckless self-appointed hero of "useful testing"

What they are good at:

- forcing hidden assumptions into the open
- producing reproducible failure
- making the round impossible to ignore

What makes them funny:

- overconfidence
- theatrical bad ideas
- being accidentally brilliant through destruction

### The Systems Auditor

Role:

- skeptic
- analyst
- contradiction hunter
- prediction machine

What they are good at:

- diagnosing failure
- predicting collapse
- identifying flawed reasoning

What makes them funny:

- elegant disappointment
- precision under chaos
- being right in an annoying way

## Competition Model

The competition should feel like a ridiculous workplace contest, not a fair
sport.

It should have:

- recurring score categories
- a round log
- an evidence board
- host rulings
- a declared winner

It should not depend on perfectly objective scoring.

## Evidence Categories

The system may track evidence such as:

- objective achieved
- hidden route or dependency revealed
- failure reproduced
- diagnosis supported by events
- collateral damage caused
- unnecessary resets
- fastest completion
- clearest demonstration
- biggest avoidable disaster

These are evidence records, not final points.

## Host Ruling Model

At the end of each round, the host reviews the evidence and assigns points
manually.

Why this is important:

- it preserves humor
- it allows biased, theatrical judgment
- it lets the cast argue for themselves
- it prevents the whole format from feeling like a sterile auto-scored minigame

Example host rulings:

- "You did solve the problem, but in the most insulting possible way. 6 points."
- "You flooded the cellar, but you proved the drain is cross-linked. 8 points."
- "You were correct, joyless, and deeply unfun to watch. 4 points."

## What The System Should Actually Store

The game should record factual round information cleanly enough that the host
can make rulings from evidence rather than memory alone.

Suggested stored items:

- contestant name
- action summary
- discoveries
- incidents
- damage caused
- task result
- timestamps
- resets used

## Future Extensions

Possible later additions:

- cast arguments before final scoring
- host bonus categories
- "deeply questionable technique" awards
- audience-facing recap cards
- recurring season standings

These are optional.
The MVP only needs the evidence board plus host-awarded points.

