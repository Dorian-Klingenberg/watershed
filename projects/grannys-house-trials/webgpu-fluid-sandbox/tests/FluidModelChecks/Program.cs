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
var cellularState = new FluidState([1], [0, 0], 1, FluidAlgorithm.Cellular);
var cellularDrift = new FluidState([1.0002f], [0, 0], 1, FluidAlgorithm.Cellular);
Assert(!FluidComparison.Compare(cellularState, cellularDrift).Matches, "cellular complete-state check retains original 0.0001 in tolerance");
var momentumOnly = new FluidState([1], [0.01f, 0], 1, FluidAlgorithm.Cellular);
var separateErrors = FluidComparison.Compare(cellularState, momentumOnly);
Assert(separateErrors.MismatchedCells == 0 && separateErrors.MaximumDifference == 0 &&
    separateErrors.MismatchedMomentumValues == 1 && separateErrors.MaximumMomentumDifference == 0.01f,
    "depth and face mismatches and errors retain separate units");

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

foreach (var algorithm in Enum.GetValues<FluidAlgorithm>())
{
    var model = new CellularFluidSimulation(9, 9, Enumerable.Range(0, 81)
        .Select(i => i % 9 is 0 or 8 || i / 9 is 0 or 8 ? 1000 : i % 3).ToArray());
    model.SelectAlgorithm(algorithm);
    var lake = model.GetStateSnapshot();
    for (var i = 0; i < 81; i++)
        lake.Water[i] = Math.Max(0, 12 - model.GetTerrainSnapshot()[i]);
    model.SynchronizeState(lake);
    for (var i = 0; i < 100; i++) model.Step();
    Assert(FluidComparison.Compare(lake.Water, model.GetWaterSnapshot(), 0.00001f).Matches, $"{algorithm} lake at rest over varied bed");
    Assert(model.GetStateSnapshot().Momentum.All(value => value == 0), $"{algorithm} resting face state");
    model.SelectAlgorithm(algorithm);
    model.AddWater(4, 4, 1, 72);
    var mass = model.GetSummary().TotalWater;
    for (var i = 0; i < 400; i++) model.Step();
    var complete = model.GetStateSnapshot();
    Assert(complete.Water.All(value => float.IsFinite(value) && value >= 0), $"{algorithm} stable nonnegative depth");
    Assert(complete.Momentum.All(float.IsFinite), $"{algorithm} finite face state");
    AssertNear(mass, complete.Water.Sum(v => (double)v), 0.01, $"{algorithm} closed basin conserves all water including film");
    var peer = new CellularFluidSimulation(9, 9, model.GetTerrainSnapshot());
    peer.SelectAlgorithm(algorithm);
    peer.SynchronizeState(complete);
    complete.Water[0] = 900;
    complete.Momentum[0] = 99;
    Assert(peer.GetStateSnapshot().Momentum[0] != 99 && peer.GetWaterSnapshot()[0] != 900, $"{algorithm} complete import copies state");
    for (var i = 0; i < 50; i++) { model.Step(); peer.Step(); }
    Assert(FluidComparison.Compare(model.GetStateSnapshot(), peer.GetStateSnapshot()).Matches, $"{algorithm} complete resume deterministic");
    if (algorithm != FluidAlgorithm.Cellular)
    {
        ExpectThrows<InvalidOperationException>(() => model.SynchronizeWater(new float[81], 0), $"{algorithm} depth-only import rejected");
        var bad = model.GetStateSnapshot();
        bad.Momentum[0] = float.NaN;
        ExpectThrows<ArgumentException>(() => model.SynchronizeState(bad), $"{algorithm} invalid momentum import rejected");
        var velocityOnly = model.GetStateSnapshot();
        velocityOnly.Momentum[0] += 1;
        Assert(!FluidComparison.Compare(model.GetStateSnapshot(), velocityOnly).Matches, $"{algorithm} comparison includes momentum");
    }
    var initialBed = model.GetTerrainSnapshot();
    model.AddBarrier([40]);
    var editedBed = model.GetTerrainSnapshot();
    var stateAfterEdit = model.GetStateSnapshot();
    Assert(new[] { 80, 81, 78, 63 }.All(edge => stateAfterEdit.Momentum[edge] == 0), $"{algorithm} edits clear all incident faces");
    model.SelectAlgorithm(FluidAlgorithm.Mac);
    Assert(model.GetTerrainSnapshot().SequenceEqual(editedBed), $"{algorithm} switching preserves edited terrain");
    Assert(model.CycleCount == 0 && model.GetStateSnapshot().Water.All(v => v == 0) &&
        model.GetStateSnapshot().Momentum.All(v => v == 0), $"{algorithm} switch clears full fluid state");
    model.Reset();
    Assert(model.Algorithm == FluidAlgorithm.Mac && model.GetTerrainSnapshot().SequenceEqual(initialBed),
        $"{algorithm} Reset retains algorithm restores initial terrain");

    var barrier = new CellularFluidSimulation(5, 5, channelTerrain);
    barrier.SelectAlgorithm(algorithm);
    barrier.AddBarrier([12]);
    barrier.AddWater(1, 2, 0, 8);
    for (var i = 0; i < 30; i++) barrier.Step();
    AssertNear(0, barrier.GetCell(2, 2).WaterDepth, 0, $"{algorithm} dry elevated neighbor blocks flow");
    barrier.AddWater(1, 2, 0, 12);
    for (var i = 0; i < 30; i++) barrier.Step();
    Assert(barrier.GetCell(2, 2).WaterDepth > 0, $"{algorithm} overtops actual terrain crest");
    var highBed = channelTerrain.Select(v => v + 100).ToArray();
    var elevated = new CellularFluidSimulation(5, 5, highBed);
    elevated.SelectAlgorithm(algorithm);
    elevated.AddWater(2, 2, 0, 12);
    elevated.Step();
    Assert(elevated.GetCell(1, 2).WaterDepth > 0, $"{algorithm} no absolute-height magic wall");
    if (algorithm != FluidAlgorithm.Cellular)
    {
        var units = new CellularFluidSimulation(5, 5, new int[25]);
        units.SelectAlgorithm(algorithm);
        units.AddCenterWater(0, 12);
        units.Step();
        var impulse = 0.016f * 32.174f;
        var rate = algorithm == FluidAlgorithm.Mac ? impulse * (1 - 0.02f * 0.016f) : impulse * 0.25f;
        if (algorithm == FluidAlgorithm.PipeFlux) rate *= 0.94f;
        if (algorithm == FluidAlgorithm.TerrainHead)
            rate /= 1 + 0.016f * 32.174f * 0.04f * 0.04f * rate / 0.25f;
        var expectedDepth = rate * 0.016f * 12 * (algorithm == FluidAlgorithm.Mac ? 1 : 0.5f);
        AssertNear(rate, units.GetStateSnapshot().Momentum[24], 0.000001, $"{algorithm} pressure impulse units");
        AssertNear(expectedDepth, units.GetCell(3, 2).WaterDepth, 0.000001, $"{algorithm} rate-to-inches continuity units");
        var movingLake = units.GetStateSnapshot();
        Array.Fill(movingLake.Water, 12);
        Array.Clear(movingLake.Momentum);
        movingLake.Momentum[24] = 0.1f;
        units.SynchronizeState(movingLake);
        units.Step();
        Assert(units.GetStateSnapshot().Momentum[24] > 0, $"{algorithm} face inertia persists without head impulse");
        var beforeAllocation = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < 20; i++) units.Step();
        Assert(GC.GetAllocatedBytesForCurrentThread() == beforeAllocation, $"{algorithm} ticks allocate no managed state");
    }
}

var configurable = CellularFluidSimulation.CreateDefault();
foreach (var settings in new[] { new FluidSettings(CellularMaxFlow: float.NaN),
    new FluidSettings(PipeArea: 0), new FluidSettings(PipeRetention: 1.1f),
    new FluidSettings(ManningRoughness: -1), new FluidSettings(MacDamping: 6) })
    ExpectThrows<ArgumentOutOfRangeException>(() => configurable.Configure(settings), "invalid settings rejected");
configurable.AddBarrier([100]);
var retainedTerrain = configurable.GetTerrainSnapshot();
configurable.AddCenterWater(1, 12);
configurable.Step();
configurable.Configure(new(MacAdvection: true));
Assert(configurable.GetTerrainSnapshot().SequenceEqual(retainedTerrain) && configurable.CycleCount == 0 &&
    configurable.GetSummary().TotalWater == 0, "settings clear fluid, preserving edited terrain");
ExpectThrows<ArgumentException>(() => configurable.SynchronizeState(configurable.GetStateSnapshot() with
    { Settings = new() }), "mismatched baseline settings rejected");

foreach (var scene in Enum.GetValues<TerrainScene>())
{
    configurable.SelectScene(scene);
    var bed = configurable.GetTerrainSnapshot();
    configurable.AddBarrier([200]);
    configurable.Reset();
    Assert(configurable.Scene == scene && bed.SequenceEqual(configurable.GetTerrainSnapshot()), $"{scene} reset restores selected terrain");
}
Assert(configurable.GetTerrainHeight(52, 14) == 0 && configurable.GetTerrainHeight(52, 24) == 48 &&
    configurable.GetTerrainHeight(52, 32) == 0, "Tesla bypass has a separated solid island, not open cavity");
Assert(configurable.GetTerrainHeight(8, 32) == configurable.GetTerrainHeight(88, 32), "comparable flat reservoir bottoms");

var advectionOff = new CellularFluidSimulation(9, 9, new int[81]);
advectionOff.SelectAlgorithm(FluidAlgorithm.Mac);
var advectionOn = new CellularFluidSimulation(9, 9, new int[81]);
advectionOn.SelectAlgorithm(FluidAlgorithm.Mac);
advectionOn.Configure(new(MacAdvection: true));
var moving = advectionOff.GetStateSnapshot();
Array.Fill(moving.Water, 12);
moving.Momentum[2 * 40] = 2;
moving.Momentum[2 * 41] = 1;
advectionOff.SynchronizeState(moving);
advectionOn.SynchronizeState(moving with { Settings = advectionOn.Settings });
advectionOff.Step(); advectionOn.Step();
Assert(!advectionOff.GetStateSnapshot().Momentum.SequenceEqual(advectionOn.GetStateSnapshot().Momentum), "advection toggle changes actual velocity evolution");

foreach (var algorithm in Enum.GetValues<FluidAlgorithm>())
{
    var experiment = CellularFluidSimulation.CreateDefault();
    experiment.SelectScene(TerrainScene.Tesla);
    experiment.SelectAlgorithm(algorithm);
    experiment.Configure(new(MacAdvection: true));
    foreach (var direction in new[] { ExperimentDirection.Forward, ExperimentDirection.Reverse })
    {
        var forcing = experiment.StartExperiment(direction, true);
        Assert(forcing.Depths[32 * 96 + 8] == (direction == ExperimentDirection.Forward ? 18 : 12) &&
            forcing.Depths[32 * 96 + 88] == (direction == ExperimentDirection.Forward ? 12 : 18), "heads swap, equal imposed difference");
        for (var tick = 0; tick < 1600; tick++) experiment.Step();
        var measured = experiment.MeasureExperiment();
        var flowUnit = algorithm == FluidAlgorithm.Cellular ? "ft3/tick" : "ft3/s";
        Console.WriteLine($"Tesla {algorithm} advection={experiment.Settings.MacAdvection} {direction} tick={experiment.CycleCount}: Q={measured.Discharge:F7} {flowUnit}; bypass={measured.BypassFlow:F7} {flowUnit}");
        AssertNear(algorithm == FluidAlgorithm.Cellular ? 1f / 12 : 1f / (12 * 0.016f),
            experiment.DischargeProbe.Weights[0], 0.000001, "probe units distinguish algorithmic ticks from physical seconds");
        Assert(experiment.GetWaterSnapshot().All(v => v >= 0 && float.IsFinite(v)), "forced experiment remains finite");
        AssertNear(forcing.Depths[32 * 96 + 8], experiment.GetCell(8, 32).WaterDepth, 0, "fixed reservoir head is maintained");
        var copy = experiment.GetStateSnapshot();
        experiment.StartExperiment(direction, true);
        for (var tick = 0; tick < 1600; tick++) experiment.Step();
        Assert(FluidComparison.Compare(copy, experiment.GetStateSnapshot()).Matches, "experiment is repeatable");
        experiment.SynchronizeState(copy);
        experiment.Step();
        AssertNear(forcing.Depths[32 * 96 + 8], experiment.GetCell(8, 32).WaterDepth, 0,
            "same-direction synchronization retains prescribed reservoir forcing");
    }
    experiment.StartExperiment(ExperimentDirection.Forward, false);
    Assert(experiment.CycleCount == 0 && experiment.GetSummary().TotalWater == 0, "CPU-off forcing does not populate frozen CPU state");
    experiment.Reset();
    Assert(experiment.Direction == ExperimentDirection.None && experiment.GetForcing().Depths.All(v => v == -1),
        "explicit fallback/reset clears forcing metadata and constraints");
}
Console.WriteLine("All fluid model checks passed (settings, advection, scenes, repeatable Tesla measurements, four-algorithm lifecycle).");

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
