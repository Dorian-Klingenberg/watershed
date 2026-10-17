using Microsoft.AspNetCore.Components;
using Microsoft.JSInterop;
using WebGpuFluidSandbox.Simulation;

namespace WebGpuFluidSandbox.Services;

public sealed class WebGpuFluidBackend(IJSRuntime jsRuntime) : IAsyncDisposable
{
    private IJSObjectReference? _module;
    private IJSObjectReference? _views;

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
        Module.InvokeVoidAsync("reset", simulation.GetTerrainSnapshot(), simulation.GetWaterSnapshot());

    public ValueTask ApplyEditsAsync(int[]? terrain, float[]? waterDelta) =>
        Module.InvokeVoidAsync("applyEdits", terrain, waterDelta);

    public async ValueTask InitializeViewsAsync<T>(
        ElementReference cpuCanvas, ElementReference gpuCanvas, ElementReference scrollbar,
        DotNetObjectReference<T> callback, CellularFluidSimulation simulation) where T : class
    {
        _views = await jsRuntime.InvokeAsync<IJSObjectReference>("import", "./js/landscape-view.js");
        await _views.InvokeVoidAsync("initialize", cpuCanvas, gpuCanvas, scrollbar, callback,
            simulation.Width, simulation.Height, simulation.GetTerrainSnapshot(), simulation.GetWaterSnapshot());
    }

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
        (_views ?? throw new InvalidOperationException("Views are not initialized."))
            .InvokeVoidAsync("renderCpu", terrain, water);

    private IJSObjectReference Module =>
        _module ?? throw new InvalidOperationException("The WebGPU module has not been initialized.");

    public async ValueTask DisposeAsync()
    {
        if (_views is not null)
        {
            await _views.InvokeVoidAsync("dispose");
            await _views.DisposeAsync();
            _views = null;
        }
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
