using WebGpuFluidSandbox.Simulation;

var terrain = new[]
{
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 10, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};
var simulation = new CellularFluidSimulation(5, 5, terrain);
simulation.AddCenterWater(radius: 0, depthInches: 8.0f);
AssertNear(8.0, simulation.GetSummary().TotalWater, 0.0001, "center pour volume");

var beforeStep = simulation.GetWaterSnapshot();
simulation.Step();
var afterStep = simulation.GetWaterSnapshot();
Assert(simulation.CycleCount == 1, "step advances the cycle");
Assert(afterStep[12] < beforeStep[12], "water leaves the raised center");
Assert(afterStep.Where((_, index) => index != 12).Any(depth => depth > 0.0f), "water reaches lower neighbors");
AssertNear(8.0, simulation.GetSummary().TotalWater, 0.0001, "closed-field water conservation");
Assert(afterStep.All(depth => depth >= 0.0f && float.IsFinite(depth)), "water depths remain finite and non-negative");

var edgePour = new CellularFluidSimulation(2, 2, new int[4]);
edgePour.AddCenterWater(radius: 1, depthInches: 2.0f);
AssertNear(6.0, edgePour.GetSummary().TotalWater, 0.0001, "center pour clips at field boundaries");
edgePour.Step();
Assert(edgePour.GetWaterSnapshot().All(depth => depth == 0.0f), "boundary water drains from the field");

var firstRun = CellularFluidSimulation.CreateDefault();
firstRun.AddCenterWater(radius: 4, depthInches: 12.0f);
var initialDefaultWater = firstRun.GetSummary().TotalWater;
var secondRun = CellularFluidSimulation.CreateDefault();
secondRun.AddCenterWater(radius: 4, depthInches: 12.0f);
for (var i = 0; i < 20; i++)
{
    firstRun.Step();
    secondRun.Step();
}

AssertNear(
    initialDefaultWater,
    firstRun.GetSummary().TotalWater,
    0.02,
    "default field retains the center pour before it reaches the boundary");
var deterministicComparison = FluidComparison.Compare(firstRun.GetWaterSnapshot(), secondRun.GetWaterSnapshot(), 0.0f);
Assert(deterministicComparison.Matches, "repeated CPU runs are deterministic");

var passingComparison = FluidComparison.Compare([1.0f, 2.0f], [1.00001f, 2.0f], 0.0001f);
Assert(passingComparison.Matches && passingComparison.MismatchedCells == 0, "comparison applies the tolerance");
var failingComparison = FluidComparison.Compare([1.0f, 2.0f], [1.01f, 2.0f], 0.0001f);
Assert(!failingComparison.Matches && failingComparison.MismatchedCells == 1, "comparison reports divergent cells");
AssertNear(0.01, failingComparison.TotalWaterDifference, 0.00001, "comparison reports total-water error");

ExpectThrows<ArgumentOutOfRangeException>(
    () => simulation.AddCenterWater(radius: -1, depthInches: 1.0f),
    "negative pour radius is rejected");
ExpectThrows<ArgumentOutOfRangeException>(
    () => simulation.AddCenterWater(radius: 1, depthInches: float.NaN),
    "non-finite water depth is rejected");
ExpectThrows<ArgumentException>(
    () => new CellularFluidSimulation(3, 3, [0, 0]),
    "terrain size must match dimensions");

var edited = CellularFluidSimulation.CreateDefault();
var originalTerrain = edited.GetTerrainSnapshot();
Assert(edited.GetCell(48, 8).TerrainHeight > edited.GetCell(48, 48).TerrainHeight, "landscape slopes downhill");
Assert(edited.GetCell(40, 37).TerrainHeight > edited.GetCell(40, 36).TerrainHeight, "default barrier obstructs flow");
Assert(edited.GetCell(46, 37).TerrainHeight < edited.GetCell(44, 37).TerrainHeight, "barrier has a lower spillway");
var delta = edited.AddWater(12, 15, 0, 3.0f);
AssertNear(3, edited.GetCell(12, 15).WaterDepth, 0, "point pour reaches selected cell");
AssertNear(3, delta.Sum(), 0, "point pour returns additive edit, not a reference snapshot");
var barrierCell = 15 * edited.Width + 12;
edited.AddBarrier([barrierCell, barrierCell]);
Assert(edited.GetCell(12, 15).TerrainHeight == originalTerrain[barrierCell] + 12, "duplicate stroke cells do not stack");
AssertNear(3, edited.GetCell(12, 15).WaterDepth, 0, "barrier preserves existing water");
ExpectThrows<ArgumentOutOfRangeException>(() => edited.AddBarrier([barrierCell + 1, -1]), "invalid barrier is rejected atomically");
Assert(edited.GetTerrainSnapshot()[barrierCell + 1] == originalTerrain[barrierCell + 1], "invalid stroke changes no cells");
ExpectThrows<ArgumentOutOfRangeException>(() => edited.AddWater(-1, 0, 0, 1), "out-of-field pour rejected");
ExpectThrows<ArgumentOutOfRangeException>(() => edited.GetCell(0, edited.Height), "out-of-field selection rejected");
edited.Step();
edited.Reset();
Assert(edited.GetTerrainSnapshot().SequenceEqual(originalTerrain), "reset restores initial landscape");
Assert(edited.CycleCount == 0 && edited.GetSummary().TotalWater == 0, "reset clears water and ticks");

var channelTerrain = Enumerable.Repeat(100, 25).ToArray();
channelTerrain[11] = channelTerrain[12] = channelTerrain[13] = 0;
var blockedChannel = new CellularFluidSimulation(5, 5, channelTerrain);
blockedChannel.AddBarrier([12]);
blockedChannel.AddWater(1, 2, 0, 8);
blockedChannel.Step();
AssertNear(8, blockedChannel.GetCell(1, 2).WaterDepth, 0, "barrier pools shallow upstream water");
AssertNear(0, blockedChannel.GetCell(2, 2).WaterDepth, 0, "water does not cross a higher barrier");
blockedChannel.AddWater(1, 2, 0, 12);
blockedChannel.Step();
Assert(blockedChannel.GetCell(2, 2).WaterDepth > 0, "water overtops a submerged barrier");
AssertNear(20, blockedChannel.GetSummary().TotalWater, 0.0001, "barrier overflow conserves water");

var suspended = new CellularFluidSimulation(5, 5, new int[25]);
suspended.AddWater(2, 2, 0, 4);
var frozen = suspended.GetWaterSnapshot();
var gpuOnlyEdit = suspended.AddWater(1, 2, 0, 7, applyToReference: false);
Assert(suspended.GetWaterSnapshot().SequenceEqual(frozen), "GPU-only pour creates a domain edit without changing frozen reference water");
AssertNear(7, gpuOnlyEdit.Sum(), 0, "GPU-only domain edit retains the same brush policy");
suspended.AddBarrier([11]);
Assert(suspended.GetCell(1, 2).TerrainHeight == 12, "terrain policy remains active without CPU stepping");
Assert(suspended.GetTerrainHeight(1, 2) == 12, "terrain-only inspection remains available without reading reference water");
ExpectThrows<ArgumentOutOfRangeException>(() => suspended.GetTerrainHeight(5, 0), "terrain-only inspection validates coordinates");
Assert(suspended.CycleCount == 0, "domain edits do not advance the disabled reference");
var baseline = new float[25];
baseline[12] = 9;
suspended.SynchronizeWater(baseline, 42);
Assert(suspended.CycleCount == 42, "explicit baseline synchronization restores GPU tick identity");
AssertNear(9, suspended.GetSummary().TotalWater, 0, "synchronization replaces frozen reference water");
baseline[12] = 100;
AssertNear(9, suspended.GetCell(2, 2).WaterDepth, 0, "baseline import does not alias caller data");
var resumedPeer = new CellularFluidSimulation(5, 5, suspended.GetTerrainSnapshot());
resumedPeer.SynchronizeWater(suspended.GetWaterSnapshot(), 42);
for (var i = 0; i < 8; i++) { suspended.Step(); resumedPeer.Step(); }
Assert(FluidComparison.Compare(suspended.GetWaterSnapshot(), resumedPeer.GetWaterSnapshot(), 0).Matches,
    "independent reference stepping resumes deterministically after synchronization");
ExpectThrows<ArgumentException>(() => suspended.SynchronizeWater([1], 42), "baseline dimension mismatch is rejected");
var invalidBaseline = suspended.GetWaterSnapshot();
invalidBaseline[0] = float.NaN;
ExpectThrows<ArgumentException>(() => suspended.SynchronizeWater(invalidBaseline, 42), "non-finite baseline is rejected atomically");
invalidBaseline[0] = -1;
ExpectThrows<ArgumentException>(() => suspended.SynchronizeWater(invalidBaseline, 42), "negative baseline is rejected");
ExpectThrows<ArgumentException>(() => suspended.SynchronizeWater(new float[25], -1), "negative baseline tick is rejected");
Assert(suspended.CycleCount == 50, "invalid synchronization leaves tick unchanged");
suspended.Step();
var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
for (var i = 0; i < 20; i++) suspended.Step();
var allocatedBySteps = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
Assert(allocatedBySteps == 0, "CPU ticks reuse their delta buffer without managed allocations");

Console.WriteLine("All fluid model checks passed.");

static void Assert(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException($"Check failed: {description}");
    }
}

static void AssertNear(double expected, double actual, double tolerance, string description)
{
    Assert(Math.Abs(expected - actual) <= tolerance, $"{description}: expected {expected}, got {actual}");
}

static void ExpectThrows<TException>(Action action, string description)
    where TException : Exception
{
    try
    {
        action();
    }
    catch (TException)
    {
        return;
    }

    throw new InvalidOperationException($"Check failed: {description}");
}
