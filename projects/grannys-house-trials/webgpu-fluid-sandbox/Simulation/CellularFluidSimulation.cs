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
    private readonly float[] _waterDepths;

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
        _waterDepths = new float[terrainHeights.Length];
    }

    public int Width { get; }
    public int Height { get; }
    public int CycleCount { get; private set; }

    public static CellularFluidSimulation CreateDefault()
    {
        const int width = 96;
        const int height = 64;
        var terrain = new int[width * height];
        var centerX = width / 2;
        var centerY = height / 2;

        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var dx = x - centerX;
                var dy = y - centerY;
                terrain[y * width + x] = (int)MathF.Floor((dx * dx + dy * dy) / 64.0f + 0.5f);
            }
        }

        return new CellularFluidSimulation(width, height, terrain);
    }

    public void Reset()
    {
        Array.Clear(_waterDepths);
        CycleCount = 0;
    }

    public void AddCenterWater(int radius, float depthInches)
    {
        if (radius < 0 || radius > Math.Max(Width, Height))
        {
            throw new ArgumentOutOfRangeException(nameof(radius));
        }

        if (!float.IsFinite(depthInches) || depthInches < 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(depthInches));
        }

        var centerX = Width / 2;
        var centerY = Height / 2;
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
                    _waterDepths[y * Width + x] += depthInches;
                }
            }
        }
    }

    public void Step()
    {
        var deltas = new float[_waterDepths.Length];
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
