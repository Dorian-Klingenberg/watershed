namespace WebGpuFluidSandbox.Simulation;

public static class FluidComparison
{
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
    bool Matches);
