namespace WebGpuFluidSandbox.Simulation;

public static class FluidComparison
{
    public static FluidComparisonResult Compare(FluidState reference, FluidState candidate)
    {
        if (reference.Direction != candidate.Direction || reference.Settings != candidate.Settings ||
            reference.Algorithm != candidate.Algorithm || reference.Cycles != candidate.Cycles)
            throw new ArgumentException("Complete states must have the same algorithm and tick.");
        var depthTolerance = reference.Algorithm == FluidAlgorithm.Cellular
            ? CellularFluidSimulation.ComparisonTolerance : CellularFluidSimulation.StateTolerance;
        var water = Compare(reference.Water, candidate.Water, depthTolerance);
        var momentum = Compare(reference.Momentum, candidate.Momentum, CellularFluidSimulation.StateTolerance);
        return water with
        {
            Matches = water.Matches && momentum.Matches,
            MomentumValuesCompared = momentum.CellsCompared,
            MismatchedMomentumValues = momentum.MismatchedCells,
            MaximumMomentumDifference = momentum.MaximumDifference
        };
    }
    public static FluidComparisonResult Compare(
        ReadOnlySpan<float> reference,
        ReadOnlySpan<float> candidate,
        float tolerance = CellularFluidSimulation.ComparisonTolerance)
    {
        if (reference.Length != candidate.Length)
        {
            throw new ArgumentException("The CPU and GPU snapshots must have the same number of cells.");
        }

        if (!float.IsFinite(tolerance) || tolerance < 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(tolerance));
        }

        var mismatchCount = 0;
        var maximumDifference = 0.0f;
        var referenceTotal = 0.0;
        var candidateTotal = 0.0;

        for (var i = 0; i < reference.Length; i++)
        {
            var expected = reference[i];
            var actual = candidate[i];
            if (!float.IsFinite(expected) || !float.IsFinite(actual))
            {
                throw new ArgumentException("Snapshots must contain only finite water depths.");
            }

            var difference = Math.Abs(expected - actual);
            maximumDifference = Math.Max(maximumDifference, difference);
            if (difference > tolerance)
            {
                mismatchCount++;
            }

            referenceTotal += expected;
            candidateTotal += actual;
        }

        return new FluidComparisonResult(
            reference.Length,
            mismatchCount,
            maximumDifference,
            Math.Abs(referenceTotal - candidateTotal),
            mismatchCount == 0);
    }
}

public sealed record FluidComparisonResult(
    int CellsCompared,
    int MismatchedCells,
    float MaximumDifference,
    double TotalWaterDifference,
    bool Matches)
{
    public int MomentumValuesCompared { get; init; }
    public int MismatchedMomentumValues { get; init; }
    public float MaximumMomentumDifference { get; init; }
}
