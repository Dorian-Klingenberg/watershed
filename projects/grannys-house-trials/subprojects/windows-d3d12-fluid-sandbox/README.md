# Windows D3D12 Fluid Sandbox

This is a small Windows-native WinUI 3 and Direct3D 12 cellular-fluid sandbox.
It is isolated in `feat/edge-d3d12-fluid-sandbox` and does not use the Blazor/WebGPU app.

## Current slice

- WinUI 3 Fluent controls: Run, Pause, Step, Pour, Reset
- 192 x 128 cellular field
- Direct3D 12 native renderer
- ARM64 Windows build
- No C# CPU-reference pane

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

On the current Qualcomm Adreno X1-85 workstation, native initialization currently reaches
`ID3D12Device::CreateRootSignature` and returns `DXGI_ERROR_DEVICE_REMOVED`
(`0x887A0005`). The device-reason query reports `0x887A0001`.

Windows Update and Winget had no matching Qualcomm/Adreno/display/graphics driver update
for the installed `31.0.137.0` driver. This remains a platform/driver diagnosis and
is not evidence that the WinUI shell or shader compilation is failing.
