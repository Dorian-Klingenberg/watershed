namespace WebGpuFluidSandbox.Simulation;

public sealed class CellularFluidSimulation
{
    public const float MaxFlowInches = 2.0f;
    public const float SettleRate = 0.5f;
    public const float MinimumWaterInches = 0.001f;
    public const float ComparisonTolerance = 0.0001f;
    public const bool DrainEdges = true;

    private const int MaximumDimension = 1024;
    private const int MaximumCellCount = 262_144;
    private readonly int[] _terrainHeights;
    private readonly int[] _initialTerrain;
    private readonly float[] _waterDepths;
    private readonly float[] _stepDeltas;

    public CellularFluidSimulation(int width, int height, int[] terrainHeights)
    {
        ArgumentNullException.ThrowIfNull(terrainHeights);
        if (width < 2 || height < 2 || width > MaximumDimension || height > MaximumDimension ||
            (long)width * height > MaximumCellCount)
        {
            throw new ArgumentOutOfRangeException(nameof(width), "The field dimensions are outside the supported range.");
        }

        if (terrainHeights.Length != width * height)
        {
            throw new ArgumentException("Terrain data must contain one value for every cell.", nameof(terrainHeights));
        }

        Width = width;
        Height = height;
        _terrainHeights = (int[])terrainHeights.Clone();
        _initialTerrain = (int[])terrainHeights.Clone();
        _waterDepths = new float[terrainHeights.Length];
        _stepDeltas = new float[terrainHeights.Length];
    }

    public int Width { get; }
    public int Height { get; }
    public int CycleCount { get; private set; }

    public static CellularFluidSimulation CreateDefault()
    {
        const int width = 96;
        const int height = 64;
        var terrain = new int[width * height];
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var channelX = width / 2.0f + 8.0f * MathF.Sin(y / 12.0f);
                var bank = MathF.Min(14.0f, MathF.Abs(x - channelX) * 0.65f);
                var slope = MathF.Max(0, 44 - y) * 0.7f + MathF.Max(0, 20 - y) * 0.6f;
                var basinRim = y > 48 ? (y - 48) * 0.65f : 0;
                var barrier = y is >= 37 and <= 38 && x is >= 30 and <= 61
                    ? (x is >= 45 and <= 48 ? 5 : 12)
                    : 0;
                terrain[y * width + x] = (int)MathF.Round(slope + bank + basinRim + barrier);
            }
        }

        return new CellularFluidSimulation(width, height, terrain);
    }

    public void Reset()
    {
        Array.Clear(_waterDepths);
        _initialTerrain.CopyTo(_terrainHeights, 0);
        CycleCount = 0;
    }

    public void AddCenterWater(int radius, float depthInches)
        => AddWater(Width / 2, Height / 2, radius, depthInches);

    public float[] AddWater(int centerX, int centerY, int radius, float depthInches, bool applyToReference = true)
    {
        if ((uint)centerX >= Width || (uint)centerY >= Height)
        {
            throw new ArgumentOutOfRangeException(nameof(centerX));
        }
        if (radius < 0 || radius > Math.Max(Width, Height))
        {
            throw new ArgumentOutOfRangeException(nameof(radius));
        }

        if (!float.IsFinite(depthInches) || depthInches < 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(depthInches));
        }

        var additions = new float[_waterDepths.Length];
        var radiusSquared = radius * radius;
        var minX = Math.Max(0, centerX - radius);
        var maxX = Math.Min(Width - 1, centerX + radius);
        var minY = Math.Max(0, centerY - radius);
        var maxY = Math.Min(Height - 1, centerY + radius);

        for (var y = minY; y <= maxY; y++)
        {
            for (var x = minX; x <= maxX; x++)
            {
                var dx = x - centerX;
                var dy = y - centerY;
                if (dx * dx + dy * dy <= radiusSquared)
                {
                    if (applyToReference) _waterDepths[y * Width + x] += depthInches;
                    additions[y * Width + x] = depthInches;
                }
            }
        }

        return additions;
    }

    public void AddBarrier(int[] cells)
    {
        ArgumentNullException.ThrowIfNull(cells);
        if (cells.Length > _terrainHeights.Length || cells.Any(index => (uint)index >= _terrainHeights.Length))
        {
            throw new ArgumentOutOfRangeException(nameof(cells));
        }

        foreach (var index in cells)
        {
            _terrainHeights[index] = (int)Math.Min(int.MaxValue, (long)_initialTerrain[index] + 12);
        }
    }

    public FluidCell GetCell(int x, int y)
    {
        var terrainHeight = GetTerrainHeight(x, y);
        return new FluidCell(x, y, terrainHeight, _waterDepths[y * Width + x]);
    }

    public int GetTerrainHeight(int x, int y)
    {
        if ((uint)x >= Width || (uint)y >= Height)
        {
            throw new ArgumentOutOfRangeException(nameof(x));
        }

        return _terrainHeights[y * Width + x];
    }

    public void Step()
    {
        var deltas = _stepDeltas;
        Array.Clear(deltas);
        Span<int> lowerIndices = stackalloc int[4];
        Span<float> lowerSurfaces = stackalloc float[4];
        Span<float> desiredInflows = stackalloc float[4];

        for (var y = 0; y < Height; y++)
        {
            for (var x = 0; x < Width; x++)
            {
                var currentIndex = y * Width + x;
                var availableWater = _waterDepths[currentIndex];
                if (availableWater <= MinimumWaterInches)
                {
                    continue;
                }

                var currentSurface = _terrainHeights[currentIndex] + availableWater;
                var lowerCount = 0;
                var surfaceSum = currentSurface;

                AddLowerNeighbor(x - 1, y, currentSurface, ref lowerCount, ref surfaceSum, lowerIndices, lowerSurfaces);
                AddLowerNeighbor(x + 1, y, currentSurface, ref lowerCount, ref surfaceSum, lowerIndices, lowerSurfaces);
                AddLowerNeighbor(x, y - 1, currentSurface, ref lowerCount, ref surfaceSum, lowerIndices, lowerSurfaces);
                AddLowerNeighbor(x, y + 1, currentSurface, ref lowerCount, ref surfaceSum, lowerIndices, lowerSurfaces);

                if (lowerCount == 0)
                {
                    continue;
                }

                var sharedSurface = surfaceSum / (lowerCount + 1);
                var desiredOutflow = 0.0f;
                for (var i = 0; i < lowerCount; i++)
                {
                    desiredInflows[i] = Math.Max(0.0f, sharedSurface - lowerSurfaces[i]);
                    desiredOutflow += desiredInflows[i];
                }

                if (desiredOutflow <= MinimumWaterInches)
                {
                    continue;
                }

                var totalFlow = Math.Min(
                    Math.Min(availableWater, MaxFlowInches),
                    desiredOutflow * SettleRate);
                if (totalFlow <= MinimumWaterInches)
                {
                    continue;
                }

                var flowScale = totalFlow / desiredOutflow;
                deltas[currentIndex] -= totalFlow;
                for (var i = 0; i < lowerCount; i++)
                {
                    deltas[lowerIndices[i]] += desiredInflows[i] * flowScale;
                }
            }
        }

        for (var i = 0; i < _waterDepths.Length; i++)
        {
            _waterDepths[i] = Math.Max(0.0f, _waterDepths[i] + deltas[i]);
        }

        if (DrainEdges)
        {
            DrainBoundary();
        }

        CycleCount++;
    }

    public int[] GetTerrainSnapshot() => (int[])_terrainHeights.Clone();
    public float[] GetWaterSnapshot() => (float[])_waterDepths.Clone();

    public void SynchronizeWater(float[] water, int cycles)
    {
        ArgumentNullException.ThrowIfNull(water);
        if (water.Length != _waterDepths.Length || cycles < 0 ||
            water.Any(depth => !float.IsFinite(depth) || depth < 0))
        {
            throw new ArgumentException("A baseline must contain finite non-negative depths and a valid tick.", nameof(water));
        }
        water.CopyTo(_waterDepths, 0);
        CycleCount = cycles;
    }

    public FluidSummary GetSummary()
    {
        var wetCellCount = 0;
        var totalWater = 0.0;
        var maxDepth = 0.0f;

        foreach (var depth in _waterDepths)
        {
            if (depth > MinimumWaterInches)
            {
                wetCellCount++;
                totalWater += depth;
                maxDepth = Math.Max(maxDepth, depth);
            }
        }

        return new FluidSummary(CycleCount, wetCellCount, totalWater, maxDepth);
    }

    private void AddLowerNeighbor(
        int x,
        int y,
        float currentSurface,
        ref int lowerCount,
        ref float surfaceSum,
        Span<int> lowerIndices,
        Span<float> lowerSurfaces)
    {
        if (x < 0 || x >= Width || y < 0 || y >= Height)
        {
            return;
        }

        var neighborIndex = y * Width + x;
        var neighborSurface = _terrainHeights[neighborIndex] + _waterDepths[neighborIndex];
        if (neighborSurface >= currentSurface - MinimumWaterInches)
        {
            return;
        }

        lowerIndices[lowerCount] = neighborIndex;
        lowerSurfaces[lowerCount] = neighborSurface;
        lowerCount++;
        surfaceSum += neighborSurface;
    }

    private void DrainBoundary()
    {
        for (var x = 0; x < Width; x++)
        {
            _waterDepths[x] = 0.0f;
            _waterDepths[(Height - 1) * Width + x] = 0.0f;
        }

        for (var y = 0; y < Height; y++)
        {
            _waterDepths[y * Width] = 0.0f;
            _waterDepths[y * Width + Width - 1] = 0.0f;
        }
    }
}

public readonly record struct FluidSummary(int Cycles, int WetCells, double TotalWater, float MaximumDepth);

public sealed record FluidCell(int X, int Y, int TerrainHeight, float WaterDepth);
