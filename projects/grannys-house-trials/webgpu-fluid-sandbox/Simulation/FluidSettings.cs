namespace WebGpuFluidSandbox.Simulation;

public sealed record FluidSettings(
    float CellularMaxFlow = 2, float CellularSettleRate = 0.5f,
    float PipeArea = 0.25f, float PipeRetention = 0.94f,
    float TerrainHeadArea = 0.25f, float ManningRoughness = 0.04f,
    float MacDamping = 0.02f, bool MacAdvection = false)
{
    public void Validate()
    {
        Check(CellularMaxFlow, 0.01f, 12);
        Check(CellularSettleRate, 0.01f, 1);
        Check(PipeArea, 0.01f, 1);
        Check(PipeRetention, 0, 1);
        Check(TerrainHeadArea, 0.01f, 1);
        Check(ManningRoughness, 0.001f, 0.2f);
        Check(MacDamping, 0, 5);
    }

    private static void Check(float value, float min, float max)
    {
        if (!float.IsFinite(value) || value < min || value > max)
            throw new ArgumentOutOfRangeException(nameof(value), $"Setting must be finite and between {min} and {max}.");
    }
}

public enum TerrainScene { Slope, EnclosedBasin, WaterSlide, Tesla, InclineFlat }
public enum ExperimentDirection { None, Forward, Reverse }
public sealed record FlowProbe(int[] Faces, float[] Weights);
public sealed record ReservoirForcing(float[] Depths, ExperimentDirection Direction);
public sealed record ExperimentReadout(double Discharge, double BypassFlow);
