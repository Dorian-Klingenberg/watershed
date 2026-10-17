using Microsoft.AspNetCore.Components;
using Microsoft.JSInterop;
using WebGpuFluidSandbox.Simulation;

namespace WebGpuFluidSandbox.Services;

public sealed class WebGpuFluidBackend(IJSRuntime jsRuntime) : IAsyncDisposable
{
    private IJSObjectReference? _module;
    private IJSObjectReference? _views;
    private readonly SemaphoreSlim _sessionGate = new(1, 1);
    private bool _sessionActive;

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

    public ValueTask ResetAsync(CellularFluidSimulation simulation, bool cpuEnabled) =>
        Module.InvokeVoidAsync("reset", simulation.GetTerrainSnapshot(),
            cpuEnabled ? simulation.GetWaterSnapshot() : new float[simulation.Width * simulation.Height]);

    public ValueTask ApplyEditsAsync(int[]? terrain, float[]? waterDelta) =>
        Module.InvokeVoidAsync("applyEdits", terrain, waterDelta);

    public async ValueTask InitializeViewsAsync<T>(
        ElementReference cpuCanvas, ElementReference gpuCanvas, ElementReference scrollbar, ElementReference verticalScrollbar,
        DotNetObjectReference<T> callback, CellularFluidSimulation simulation) where T : class
    {
        // A remounted page must not initialize over the previous page's async cleanup.
        await _sessionGate.WaitAsync();
        _sessionActive = true;
        _views = await jsRuntime.InvokeAsync<IJSObjectReference>("import", "./js/landscape-view.js");
        await _views.InvokeVoidAsync("initialize", cpuCanvas, gpuCanvas, scrollbar, verticalScrollbar, callback,
            simulation.Width, simulation.Height, simulation.GetTerrainSnapshot(), simulation.GetWaterSnapshot());
    }

    public ValueTask StepAsync(int ticks) =>
        Module.InvokeVoidAsync("step", ticks);

    public ValueTask<GpuBaseline> GetSnapshotAsync(bool fresh = false) =>
        Module.InvokeAsync<GpuBaseline>("snapshot", fresh);

    public ValueTask SetCpuEnabledAsync(bool enabled) =>
        Views.InvokeVoidAsync("setCpuEnabled", enabled);

    public ValueTask SetGpuEnabledAsync(bool enabled) =>
        Views.InvokeVoidAsync("setGpuEnabled", enabled);

    public ValueTask<bool> WaitForDisplayAsync() =>
        Views.InvokeAsync<bool>("waitForDisplay");

    public ValueTask CancelPendingWaitsAsync() =>
        _views?.InvokeVoidAsync("cancelPendingWaits") ?? ValueTask.CompletedTask;

    public ValueTask RenderCpuAsync(
        ElementReference canvas,
        int[]? terrain,
        float[] water,
        int width,
        int height) =>
        Views.InvokeVoidAsync("renderCpu", terrain, water);

    private IJSObjectReference Views =>
        _views ?? throw new InvalidOperationException("Views are not initialized.");

    private IJSObjectReference Module =>
        _module ?? throw new InvalidOperationException("The WebGPU module has not been initialized.");

    public async ValueTask DisposeAsync()
    {
        if (!_sessionActive) return;
        _sessionActive = false;
        var views = _views;
        var module = _module;
        _views = null;
        _module = null;
        try
        {
            try
            {
                if (views is not null)
                {
                    try { await views.InvokeVoidAsync("dispose"); }
                    finally { await views.DisposeAsync(); }
                }
            }
            finally
            {
                if (module is not null)
                {
                    try { await module.InvokeVoidAsync("dispose"); }
                    finally { await module.DisposeAsync(); }
                }
            }
        }
        finally
        {
            _sessionGate.Release();
        }
    }
}

public sealed record GpuInitializationResult(bool Supported, string Message);
public sealed record GpuBaseline(float[] Water, int Cycles);
