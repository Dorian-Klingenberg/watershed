# Windows D3D12 Fluid Sandbox

This is the Windows-native WinUI 3 and Direct3D 12 presentation of the Fluid
Algorithm Lab reference. It links the same `CellularFluidSimulation` domain
model as the WebGPU app; Direct3D 12 owns the swap chain and terrain view.
The shared C# model remains the fluid solver. The native renderer rasterizes
the current terrain/water snapshot and presents it through a D3D12 swap chain;
this is not a D3D12 compute-solver port. The optional comparison model is an
independent clone of the shared C# model.

## Current slice

- All four current algorithms: Cellular, Pipe-flux shallow water, Terrain-head
  pipe flow, and Staggered MAC with optional upwind advection
- Run/Pause, one-tick Step, 1/2/4/8 ticks per display, pour, reset, a cloned CPU
  reference state, and state comparison
- Five terrain scenes, spring sources, 12-inch right-drag raise/carve strokes,
  versioned map save/restore/import/export, and Tesla forward/reverse trials
- Water totals, wet-cell counts, max depth, solver timing, render/upload timing,
  and ticks/second readouts
- One height-aware projected terrain view with shared hover, selection, and
  spring markers
- Full six-degree camera controls: X/Y/Z focus, yaw, elevation, and roll
- Left click selects; left drag orbits from the exact picked world point;
  right-click increments a spring; right-drag previews then commits a terrain
  stroke; the wheel zooms continuously around its picked world point
- WebGPU-compatible projection and positive screen-down/field-Y mapping
- Cursor-anchored zoom from 0.4x to 12x and aspect-correct resize behavior
- 192 x 128 terrain field
- Direct3D 12 swap chain and native presentation
- ARM64 Windows build

The shared C# model is the fluid solver. The D3D12 app renders its primary
snapshot and runs an independent cloned reference model for comparison; this
slice does not implement D3D12 compute shaders or the WebGPU app's second,
side-by-side CPU canvas.

## Build

```powershell
cmake --preset vs2026-arm64-debug
cmake --build --preset build-vs2026-arm64-debug --target windows_d3d12_fluid_native
dotnet build projects/grannys-house-trials/subprojects/windows-d3d12-fluid-sandbox/WindowsD3D12FluidSandbox.csproj --configuration Debug --runtime win-arm64
```

## Run

```powershell
.\projects\grannys-house-trials\subprojects\windows-d3d12-fluid-sandbox\bin\Debug\net10.0-windows10.0.26100.0\win-arm64\WindowsD3D12FluidSandbox.exe
```

## Local diagnostic status

The native initialization sequence now passes on the Qualcomm Adreno X1-85 workstation:

- D3D12 11.0 device and composition swap chain initialize successfully.
- The exact SRV root signature and graphics PSO create successfully.
- The 192 x 128 field texture, upload buffer, RTV heap, and SRV create successfully.
- The first reset/render no longer emits `E_INVALIDARG`.
- The app reports `D3D12 ready` and is usable.

The detailed stage report found and fixed two initialization defects:

1. `D3D12_SHADER_RESOURCE_VIEW_DESC` needed explicit
   `D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING`.
2. The first render emitted a redundant `COPY_DEST -> COPY_DEST` resource-barrier
   transition; same-state transitions are now omitted.

The camera parity rewrite also restored the native `NativePickCell` export.
Without it, a pointer press raised `EntryPointNotFoundException` across the
WinUI event callback and Windows reported `Microsoft.UI.Xaml.dll` `0xc000027b`.
The export and managed pick-call guards are now present; the rebuilt ARM64 app
stays open during orbit, zoom, spring, and terrain-stroke interaction checks.
