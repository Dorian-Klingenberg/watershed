using Microsoft.AspNetCore.Components;
using Microsoft.JSInterop;
using WebGpuFluidSandbox.Simulation;

namespace WebGpuFluidSandbox.Services;

public sealed class WebGpuFluidBackend(IJSRuntime jsRuntime) : IAsyncDisposable
{
    private IJSObjectReference? _module;

    public async Task<GpuInitializationResult> InitializeAsync(
        ElementReference canvas,
        CellularFluidSimulation simulation)
    {
        _module = await jsRuntime.InvokeAsync<IJSObjectReference>("import", "./js/fluid-gpu.js");
        return await _module.InvokeAsync<GpuInitializationResult>(
            "initialize",
            canvas,
            simulation.Width,
            simulation.Height,
            simulation.GetTerrainSnapshot(),
            simulation.GetWaterSnapshot(),
            CellularFluidSimulation.MaxFlowInches,
            CellularFluidSimulation.SettleRate,
            CellularFluidSimulation.MinimumWaterInches,
            CellularFluidSimulation.DrainEdges);
    }

    public ValueTask ResetAsync(CellularFluidSimulation simulation) =>
        Module.InvokeVoidAsync("reset", simulation.GetWaterSnapshot());

    public ValueTask SetWaterAsync(float[] water) =>
        Module.InvokeVoidAsync("setWater", water);

    public ValueTask StepAsync() =>
        Module.InvokeVoidAsync("step");

    public ValueTask<float[]> GetSnapshotAsync() =>
        Module.InvokeAsync<float[]>("snapshot");

    public ValueTask RenderCpuAsync(
        ElementReference canvas,
        int[] terrain,
        float[] water,
        int width,
        int height) =>
        Module.InvokeVoidAsync("renderCpu", canvas, terrain, water, width, height);

    private IJSObjectReference Module =>
        _module ?? throw new InvalidOperationException("The WebGPU module has not been initialized.");

    public async ValueTask DisposeAsync()
    {
        var module = _module;
        _module = null;
        if (module is not null)
        {
            try
            {
                await module.InvokeVoidAsync("dispose");
            }
            finally
            {
                await module.DisposeAsync();
            }
        }
    }
}

public sealed record GpuInitializationResult(bool Supported, string Message);
