using WebGpuFluidSandbox.Simulation;

var terrain = new[]
{
    0, 0, 0,
    0, 10, 0,
    0, 0, 0
};
var simulation = new CellularFluidSimulation(3, 3, terrain);
simulation.AddCenterWater(radius: 0, depthInches: 8.0f);
AssertNear(8.0, simulation.GetSummary().TotalWater, 0.0001, "center pour volume");

var beforeStep = simulation.GetWaterSnapshot();
simulation.Step();
var afterStep = simulation.GetWaterSnapshot();
Assert(simulation.CycleCount == 1, "step advances the cycle");
Assert(afterStep[4] < beforeStep[4], "water leaves the raised center");
Assert(afterStep.Where((_, index) => index != 4).Any(depth => depth > 0.0f), "water reaches lower neighbors");
AssertNear(8.0, simulation.GetSummary().TotalWater, 0.0001, "closed-field water conservation");
Assert(afterStep.All(depth => depth >= 0.0f && float.IsFinite(depth)), "water depths remain finite and non-negative");

var edgePour = new CellularFluidSimulation(2, 2, new int[4]);
edgePour.AddCenterWater(radius: 1, depthInches: 2.0f);
AssertNear(6.0, edgePour.GetSummary().TotalWater, 0.0001, "center pour clips at field boundaries");

var firstRun = CellularFluidSimulation.CreateDefault();
firstRun.AddCenterWater(radius: 4, depthInches: 12.0f);
var secondRun = CellularFluidSimulation.CreateDefault();
secondRun.AddCenterWater(radius: 4, depthInches: 12.0f);
for (var i = 0; i < 20; i++)
{
    firstRun.Step();
    secondRun.Step();
}

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
