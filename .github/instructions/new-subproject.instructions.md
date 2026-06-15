---
description: Rules for adding new GHT subprojects to the watershed monorepo so they are immediately buildable and debuggable in VS Code.
---

# Adding a New GHT Subproject

## Build wiring

1. Add `add_subdirectory({name})` to `projects/grannys-house-trials/subprojects/CMakeLists.txt`.

2. The new subproject's own `CMakeLists.txt` must **not** contain `cmake_minimum_required` or `project()`.
   Those declarations belong only in the root `CMakeLists.txt` (`D:\Repos\watershed\CMakeLists.txt`).
   Start the file directly with `find_program(...)` or `add_executable(...)`.

3. Set the MSVC debugger working directory to the repo root so relative paths resolve correctly:
   ```cmake
   set_target_properties({target_name} PROPERTIES
       VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
   )
   ```

4. Group the target under the correct VS solution folder:
   ```cmake
   set_property(TARGET {target_name} PROPERTY FOLDER "Granny's House Trials/Subprojects")
   ```

## VS Code launch configuration

**This repo uses hand-authored `.vscode/tasks.json` and `.vscode/launch.json`.** VS Code's Run & Debug dropdown is NOT populated automatically by CMake Tools — every new subproject must be added manually.

First add a per-target build task to `.vscode/tasks.json`:

```json
{
  "label": "build: {target_name}",
  "type": "shell",
  "command": "cmake --build --preset build-vs2026-debug --target {target_name}",
  "options": { "cwd": "${workspaceFolder}" },
  "presentation": { "reveal": "always", "panel": "shared" },
  "problemMatcher": "$msCompile"
}
```

Then add a launch configuration to `.vscode/launch.json`:

```json
{
  "name": "[GHT] {Display Name}",
  "type": "cppvsdbg",
  "request": "launch",
  "program": "${workspaceFolder}/build/vs2026-debug/projects/grannys-house-trials/subprojects/{name}/Debug/{target_name}.exe",
  "args": [],
  "cwd": "${workspaceFolder}",
  "stopAtEntry": false,
  "environment": [],
  "preLaunchTask": "build: {target_name}"
}
```

Key points:
- `cwd` must be `${workspaceFolder}` (the watershed root). This matches `VS_DEBUGGER_WORKING_DIRECTORY` in CMake.
- `type` is `cppvsdbg` (MSVC debugger).
- `preLaunchTask` must match the `label` of the per-target task exactly.
- Build output lives under `build/vs2026-debug/` (VS2026 preset), not `build/`.
- Each launch config gets its own task that builds only that target — F5 is incremental.
- Use `"console": "integratedTerminal"` for non-WIN32 (console) executables.

## Checklist for a new subproject

- [ ] `projects/grannys-house-trials/subprojects/CMakeLists.txt` — add `add_subdirectory({name})`
- [ ] `projects/grannys-house-trials/subprojects/{name}/CMakeLists.txt` — no `cmake_minimum_required` or `project()`; has `VS_DEBUGGER_WORKING_DIRECTORY` and `FOLDER` property
- [ ] `.vscode/tasks.json` (watershed root) — `build: {target_name}` task added
- [ ] `.vscode/launch.json` (watershed root) — new launch configuration added with correct build output path
- [ ] Build confirms zero errors/warnings before continuing
