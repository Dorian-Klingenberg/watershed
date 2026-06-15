---
description: Rules for managing source files as the grass-field-003 learning project advances through its numbered steps.
applyTo: 'projects/grannys-house-trials/subprojects/grass-field-003/**'
---

# grass-field-003 Step Progression Rules

## The rule

Each step in the learning curriculum lives in its **own permanently preserved source file**.

When advancing from Step N to Step N+1:
1. **Copy** the current `main.cpp` to `main_step{N}.cpp` — this locks in the completed lesson.
2. **Create** a fresh `main_step{N+1}.cpp` (or rename the working file) as the new active file.
3. **Never modify** a completed step file (`main_step1.cpp`, `main_step2.cpp`, …). They are read-only curriculum artifacts.
4. Update `CMakeLists.txt` to compile the new active step file.

## Why

The numbered files serve as a diff-able textbook. A learner (or future agent) can open `main_step1.cpp` and `main_step2.cpp` side-by-side to see exactly what D3D12 initialization added on top of the bare Win32 window — without any "before" state being overwritten.

## File naming convention

| File | Contents |
|---|---|
| `main_step1.cpp` | Step 1 completed — bare Win32 window (frozen) |
| `main_step2.cpp` | Step 2 completed — D3D12 device + swap chain (frozen) |
| `main_step{N}.cpp` | Step N completed (frozen) |
| `main.cpp` | Current active working file (the step in progress) |

## CMakeLists.txt update pattern

When advancing to Step N+1, change the source file listed in `add_executable`:

```cmake
add_executable(
    grannys_house_trials_grass_field_003
    WIN32
        main_step{N+1}.cpp   # ← updated; previous step file left untouched
)
```

## Checklist when advancing a step

- [ ] Copy `main.cpp` → `main_stepN.cpp` (do NOT delete or edit afterwards)
- [ ] Start the new step in `main.cpp` (or a new `main_step{N+1}.cpp`)
- [ ] Update `CMakeLists.txt` source file reference
- [ ] Confirm the build is clean before writing new Step N+1 code
- [ ] Update `PLAN.md` to mark Step N complete and note what was learned
