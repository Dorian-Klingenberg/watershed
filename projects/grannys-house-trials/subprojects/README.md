# Subprojects

This folder is reserved for future focused slices that reuse the shared
project code from the main scaffold.

The goal is:

- one small shared project library compiles once
- subprojects stay thin and link that shared code
- scenario truth stays in `sim`
- tester-facing protocol stays in `playtest`
- platform-specific windowing and rendering stay local to the subproject

Subprojects should be thin.
They should prove a focused slice, not fork the project's core concepts or
duplicate shared logic.

Current subprojects:

- [grass-field-001](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-001/README.md)
