# Windows D3D12 Fluid Sandbox

This is a small Windows-native WinUI 3 and Direct3D 12 cellular-fluid sandbox.
It is isolated in `feat/edge-d3d12-fluid-sandbox` and does not use the Blazor/WebGPU app.

## Current slice

- WinUI 3 Fluent left-rail controls: Run, Pause, Step, Pour, Reset
- Tick batches: 1, 2, 4, and 8
- Independent native CPU reference state and mismatch readout
- Pour depth and cellular max-flow/settle controls
- Top-down and projected-column views
- Zoom, horizontal/vertical position, and selection
- Raise, carve, and select terrain tools
- 192 x 128 cellular field
- Direct3D 12 native renderer
- ARM64 Windows build

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

Windows WER may still report an idle/close-time
`Microsoft.UI.Xaml.dll` `RPC_E_DISCONNECTED (0x8001010E)` / `c000027b`.
That lifetime report is separate from the resolved initialization and resource
sequence and remains under investigation.
