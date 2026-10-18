using System.Text.Json;

namespace WebGpuFluidSandbox.Simulation;

public sealed class CellularFluidSimulation
{
    public const float MaxFlowInches = 2.0f;
    public const float SettleRate = 0.5f;
    public const float MinimumWaterInches = 0.001f;
    public const float ComparisonTolerance = 0.0001f;
    public const bool DrainEdges = true;
    public const float TimeStepSeconds = 0.016f;
    public const float Gravity = 32.174f;
    public const float StateTolerance = 0.002f;

    private const int MaximumDimension = 1024;
    private const int MaximumCellCount = 262_144;
    public const int StandardWidth = 192;
    public const int StandardHeight = 128;
    private static readonly Lazy<int[]> TeslaTerrain = new(LoadTeslaTerrain);
    private readonly int[] _terrainHeights;
    private readonly int[] _initialTerrain;
    private readonly float[] _waterDepths;
    private readonly float[] _stepDeltas;
    private readonly float[] _momentum;
    private readonly float[] _proposed;
    private readonly float[] _transfers;
    private readonly float[] _scales;
    private readonly float[] _forcing;
    private readonly float[] _springRates;
    public const float SpringFlowIncrement = 0.25f;
    public const float MaximumSpringFlow = 1000f;

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
        _momentum = new float[terrainHeights.Length * 2];
        _proposed = new float[_momentum.Length];
        _transfers = new float[_momentum.Length];
        _scales = new float[terrainHeights.Length];
        _forcing = Enumerable.Repeat(-1f, terrainHeights.Length).ToArray();
        _springRates = new float[terrainHeights.Length];
    }

    public int Width { get; }
    public int Height { get; }
    public int CycleCount { get; private set; }
    public FluidAlgorithm Algorithm { get; private set; }
    public FluidSettings Settings { get; private set; } = new();
    public TerrainScene Scene { get; private set; }
    public ExperimentDirection Direction { get; private set; }
    public (int X, int Y) PourLocation => Scene switch
    {
        TerrainScene.EnclosedBasin => (Width / 2, Height / 2),
        TerrainScene.WaterSlide => ((int)Math.Round(48 + 18 * Math.Sin(12 / 11.0)), 12),
        TerrainScene.Tesla => (8, 64),
        _ => (Width / 2, 24)
    };

    public void Configure(FluidSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        settings.Validate();
        Settings = settings;
        ClearFluid();
    }

    public void SelectAlgorithm(FluidAlgorithm algorithm)
    {
        if (!Enum.IsDefined(algorithm)) throw new ArgumentOutOfRangeException(nameof(algorithm));
        Algorithm = algorithm;
        ClearFluid();
    }

    private void ClearFluid()
    {
        Array.Clear(_waterDepths);
        Array.Clear(_momentum);
        Array.Clear(_transfers);
        Array.Fill(_forcing, -1);
        Direction = ExperimentDirection.None;
        CycleCount = 0;
    }

    public static CellularFluidSimulation CreateDefault()
    {
        const int width = StandardWidth;
        const int height = StandardHeight;
        var terrain = new int[width * height];
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var channelX = width / 2.0f + 16.0f * MathF.Sin(y / 24.0f);
                var bank = MathF.Min(28.0f, MathF.Abs(x - channelX) * 0.65f);
                var slope = MathF.Max(0, 88 - y) * 0.35f + MathF.Max(0, 40 - y) * 0.3f;
                var basinRim = y > 96 ? (y - 96) * 0.325f : 0;
                var barrier = y is >= 74 and <= 76 && x is >= 60 and <= 123
                    ? (x is >= 90 and <= 96 ? 5 : 12)
                    : 0;
                terrain[y * width + x] = (int)MathF.Round(slope + bank + basinRim + barrier);
            }
        }

        return new CellularFluidSimulation(width, height, terrain);
    }

    public void Reset()
    {
        ClearFluid();
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
            var height = (int)Math.Min(int.MaxValue, (long)_initialTerrain[index] + 12);
            if (_terrainHeights[index] == height) continue;
            _terrainHeights[index] = height;
            _momentum[2 * index] = _momentum[2 * index + 1] = 0;
            if (index % Width > 0) _momentum[2 * (index - 1)] = 0;
            if (index >= Width) _momentum[2 * (index - Width) + 1] = 0;
        }
    }

    public void CarveChannel(int[] cells)
    {
        ValidateTerrainCells(cells);
        foreach (var index in cells.Distinct())
        {
            _terrainHeights[index] = Math.Max(0, _terrainHeights[index] - 12);
            ClearAdjacentMomentum(index);
        }
    }

    public float AddSpring(int x, int y)
    {
        GetTerrainHeight(x, y);
        var index = y * Width + x;
        var next = _springRates[index] + SpringFlowIncrement;
        if (next > MaximumSpringFlow) throw new InvalidOperationException("This spring has reached its maximum flow rate.");
        return _springRates[index] = next;
    }

    public float GetSpringRate(int x, int y)
    {
        GetTerrainHeight(x, y);
        return _springRates[y * Width + x];
    }

    public void RemoveSpring(int x, int y)
    {
        GetTerrainHeight(x, y);
        _springRates[y * Width + x] = 0;
    }

    public void ClearSprings() => Array.Clear(_springRates);
    public float[] GetSpringRates() => (float[])_springRates.Clone();
    public SpringSource[] GetSpringSnapshot() => _springRates.Select((rate, i) => (rate, i))
        .Where(item => item.rate > 0).Select(item => new SpringSource(item.i % Width, item.i / Width, item.rate)).ToArray();

    private float[] ValidateSprings(SpringSource[]? springs)
    {
        var rates = new float[_springRates.Length];
        if (springs is null) return rates;
        if (springs.Length > rates.Length) throw new ArgumentException("Too many springs.", nameof(springs));
        foreach (var source in springs)
        {
            if (source is null || (uint)source.X >= Width || (uint)source.Y >= Height ||
                !float.IsFinite(source.FlowCubicFeetPerSecond) || source.FlowCubicFeetPerSecond <= 0 ||
                source.FlowCubicFeetPerSecond > MaximumSpringFlow)
                throw new ArgumentException("Spring positions and flow rates must be valid.", nameof(springs));
            var index = source.Y * Width + source.X;
            if (rates[index] != 0) throw new ArgumentException("Each cell can contain only one spring.", nameof(springs));
            rates[index] = source.FlowCubicFeetPerSecond;
        }
        return rates;
    }

    private void ApplySprings()
    {
        for (var i = 0; i < _waterDepths.Length; i++)
            _waterDepths[i] += _springRates[i] * TimeStepSeconds * 12;
    }

    public void LoadTerrainSnapshot(int[] terrain, SpringSource[]? springs = null)
    {
        ArgumentNullException.ThrowIfNull(terrain);
        if (terrain.Length != _terrainHeights.Length || terrain.Any(height => height is < 0 or > 4096))
            throw new ArgumentOutOfRangeException(nameof(terrain), "Map elevations must contain one 0–4096 in value per cell.");
        var springRates = ValidateSprings(springs);
        springRates.CopyTo(_springRates, 0);
        terrain.CopyTo(_initialTerrain, 0);
        terrain.CopyTo(_terrainHeights, 0);
        Scene = TerrainScene.InclineFlat;
        ClearFluid();
    }

    private void ValidateTerrainCells(int[] cells)
    {
        ArgumentNullException.ThrowIfNull(cells);
        if (cells.Length > _terrainHeights.Length || cells.Any(index => (uint)index >= _terrainHeights.Length))
            throw new ArgumentOutOfRangeException(nameof(cells));
    }

    private void ClearAdjacentMomentum(int index)
    {
        _momentum[2 * index] = _momentum[2 * index + 1] = 0;
        if (index % Width > 0) _momentum[2 * (index - 1)] = 0;
        if (index >= Width) _momentum[2 * (index - Width) + 1] = 0;
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
        if (Algorithm != FluidAlgorithm.Cellular)
        {
            StepMomentum();
            ApplyForcing();
            ApplySprings();
            CycleCount++;
            return;
        }
        var deltas = _stepDeltas;
        Array.Clear(deltas);
        Array.Clear(_transfers);
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
                    Math.Min(availableWater, Settings.CellularMaxFlow),
                    desiredOutflow * Settings.CellularSettleRate);
                if (totalFlow <= MinimumWaterInches)
                {
                    continue;
                }

                var flowScale = totalFlow / desiredOutflow;
                deltas[currentIndex] -= totalFlow;
                for (var i = 0; i < lowerCount; i++)
                {
                    deltas[lowerIndices[i]] += desiredInflows[i] * flowScale;
                    var neighbor = lowerIndices[i];
                    var edge = neighbor > currentIndex ? 2 * currentIndex + (neighbor == currentIndex + 1 ? 0 : 1)
                        : 2 * neighbor + (neighbor == currentIndex - 1 ? 0 : 1);
                    _transfers[edge] += (neighbor > currentIndex ? 1 : -1) * desiredInflows[i] * flowScale;
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
        ApplyForcing();
        ApplySprings();
    }

    public int[] GetTerrainSnapshot() => (int[])_terrainHeights.Clone();
    public float[] GetWaterSnapshot() => (float[])_waterDepths.Clone();
    public FluidState GetStateSnapshot() => new(GetWaterSnapshot(), (float[])_momentum.Clone(), CycleCount, Algorithm)
        { Settings = Settings, Direction = Direction };

    public void SynchronizeState(FluidState state)
    {
        if (state.Direction != Direction || state.Settings != Settings || state.Algorithm != Algorithm || state.Momentum.Length != _momentum.Length ||
            state.Momentum.Any(value => !float.IsFinite(value)))
            throw new ArgumentException("Invalid algorithm or momentum baseline.", nameof(state));
        ImportWater(state.Water, state.Cycles);
        state.Momentum.CopyTo(_momentum, 0);
    }

    public void SynchronizeWater(float[] water, int cycles)
    {
        if (Algorithm != FluidAlgorithm.Cellular)
            throw new InvalidOperationException("Momentum solvers require a complete state baseline.");
        ImportWater(water, cycles);
    }

    private void ImportWater(float[] water, int cycles)
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
            totalWater += depth;
            maxDepth = Math.Max(maxDepth, depth);
            if (depth > MinimumWaterInches)
            {
                wetCellCount++;
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

    private float Transfer(int edge) => edge < 0 ? 0 : _transfers[edge];

    private void StepMomentum()
    {
        // Each cell owns its east and south face. Outer faces are implicit zero.
        for (var i = 0; i < _waterDepths.Length; i++)
        {
            for (var axis = 0; axis < 2; axis++)
            {
                var edge = 2 * i + axis;
                var neighbor = axis == 0 ? (i % Width + 1 < Width ? i + 1 : -1)
                    : (i + Width < _waterDepths.Length ? i + Width : -1);
                _proposed[edge] = _transfers[edge] = 0;
                if (neighbor < 0) continue;
                var crest = Math.Max(_terrainHeights[i], _terrainHeights[neighbor]);
                var left = Math.Max(0, (_terrainHeights[i] - crest + _waterDepths[i]) / 12);
                var right = Math.Max(0, (_terrainHeights[neighbor] - crest + _waterDepths[neighbor]) / 12);
                if (left == 0 && right == 0) continue;
                var head = left - right;
                var old = _momentum[edge];
                var area = Algorithm == FluidAlgorithm.Mac ? 1
                    : Algorithm == FluidAlgorithm.TerrainHead ? Settings.TerrainHeadArea : Settings.PipeArea;
                var advected = Algorithm == FluidAlgorithm.Mac && Settings.MacAdvection ? Advect(i, axis) : old;
                var next = advected + TimeStepSeconds * area * Gravity * head;
                var donor = next >= 0 ? left : right;
                if (donor <= 0) continue;
                if (Algorithm == FluidAlgorithm.TerrainHead)
                {
                    // Manning-style semi-implicit rough-bed loss; radius in feet.
                    var radius = Math.Max(donor, 0.001f);
                    var radiusPower = MathF.Pow(radius, 4.0f / 3);
                    next /= 1 + TimeStepSeconds * Gravity * Settings.ManningRoughness * Settings.ManningRoughness * Math.Abs(next) / (area * radiusPower);
                }
                else next *= Algorithm == FluidAlgorithm.Mac ? 1 - Settings.MacDamping * TimeStepSeconds : Settings.PipeRetention;
                var speedLimit = Math.Max(0, 0.45f / TimeStepSeconds - MathF.Sqrt(Gravity * Math.Max(left, right)));
                var limit = speedLimit * (Algorithm == FluidAlgorithm.Mac ? 1 : area);
                next = Math.Clamp(next, -limit, limit);
                _proposed[edge] = next;
                // MAC transports upwind depth, pipes integrate old/new volume rates.
                var transfer = Algorithm == FluidAlgorithm.Mac ? next * donor * TimeStepSeconds * 12
                    : (old + next) * 0.5f * TimeStepSeconds * 12;
                if ((transfer > 0 && left > 0) || (transfer < 0 && right > 0))
                    _transfers[edge] = transfer;
            }

        }
        for (var i = 0; i < _waterDepths.Length; i++)
        {
            var west = i % Width > 0 ? 2 * (i - 1) : -1;
            var north = i >= Width ? 2 * (i - Width) + 1 : -1;
            var outgoing = Math.Max(0, -Transfer(west)) + Math.Max(0, Transfer(2 * i)) +
                Math.Max(0, -Transfer(north)) + Math.Max(0, Transfer(2 * i + 1));
            _scales[i] = outgoing > 0 ? Math.Min(1, _waterDepths[i] * 0.25f / outgoing) : 1;
        }
        for (var i = 0; i < _waterDepths.Length; i++)
        {
            for (var axis = 0; axis < 2; axis++)
            {
                var edge = 2 * i + axis;
                var neighbor = axis == 0 ? i + 1 : i + Width;
                var scale = _transfers[edge] >= 0 ? _scales[i] : _scales[neighbor];
                _transfers[edge] *= scale;
                _momentum[edge] = _proposed[edge] * scale;
            }
        }
        for (var i = 0; i < _waterDepths.Length; i++)
        {
            var delta = i % Width > 0 ? Transfer(2 * (i - 1)) : 0;
            delta -= Transfer(2 * i);
            if (i >= Width) delta += Transfer(2 * (i - Width) + 1);
            delta -= Transfer(2 * i + 1);
            _waterDepths[i] = Math.Max(0, _waterDepths[i] + delta);
        }
        if (DrainEdges) DrainBoundary();
    }

    private float Face(int x, int y, int axis)
        => x < 0 || y < 0 || x >= Width || y >= Height ||
           (axis == 0 && x == Width - 1) || (axis == 1 && y == Height - 1)
            ? 0 : _momentum[2 * (y * Width + x) + axis];

    private float Advect(int i, int axis)
    {
        var x = i % Width;
        var y = i / Width;
        var u = axis == 0 ? Face(x, y, 0) :
            (Face(x, y, 0) + Face(x - 1, y, 0) + Face(x, y + 1, 0) + Face(x - 1, y + 1, 0)) * 0.25f;
        var v = axis == 1 ? Face(x, y, 1) :
            (Face(x, y, 1) + Face(x + 1, y, 1) + Face(x, y - 1, 1) + Face(x + 1, y - 1, 1)) * 0.25f;
        var q = Face(x, y, axis);
        var dx = u >= 0 ? q - Face(x - 1, y, axis) : Face(x + 1, y, axis) - q;
        var dy = v >= 0 ? q - Face(x, y - 1, axis) : Face(x, y + 1, axis) - q;
        return q - TimeStepSeconds * (u * dx + v * dy);
    }

    public void SelectScene(TerrainScene scene)
    {
        if (!Enum.IsDefined(scene)) throw new ArgumentOutOfRangeException(nameof(scene));
        if (Width != StandardWidth || Height != StandardHeight)
            throw new InvalidOperationException($"Scenes require the standard {StandardWidth} × {StandardHeight} field.");
        ClearSprings();
        var slope = scene == TerrainScene.Slope ? CreateDefault().GetTerrainSnapshot() : null;
        var tesla = scene == TerrainScene.Tesla ? TeslaTerrain.Value : null;
        for (var y = 0; y < Height; y++)
        for (var x = 0; x < Width; x++)
        {
            var index = y * Width + x;
            var z = tesla?[index] ?? slope?[index] ?? 48;
            if (scene == TerrainScene.EnclosedBasin)
                z = x is >= 16 and <= 175 && y is >= 16 and <= 111 ? 0 : 36;
            if (scene == TerrainScene.WaterSlide)
                z = Math.Abs(x - (96 + 36 * Math.Sin(y / 22.0))) < 14 ? Math.Max(0, 96 - y) : 64;
            if (scene == TerrainScene.InclineFlat)
                z = x < 72 ? (int)Math.Round(24.0 * (71 - x) / 71) : 0;
            _initialTerrain[y * Width + x] = z;
        }
        Scene = scene;
        Reset();
    }

    private static int[] LoadTeslaTerrain()
    {
        using var stream = typeof(CellularFluidSimulation).Assembly
            .GetManifestResourceStream("TeslaTerrain.json")
            ?? throw new InvalidOperationException("The stamped Tesla terrain asset is missing from the application.");
        using var document = JsonDocument.Parse(stream);
        var map = document.RootElement;
        if (map.GetProperty("Version").GetInt32() != 1 ||
            map.GetProperty("Width").GetInt32() != StandardWidth ||
            map.GetProperty("Height").GetInt32() != StandardHeight)
            throw new InvalidDataException($"The Tesla terrain asset must be version 1 and {StandardWidth} × {StandardHeight} cells.");

        var elevations = map.GetProperty("ElevationInches").EnumerateArray()
            .Select(cell => cell.GetInt32()).ToArray();
        if (elevations.Length != StandardWidth * StandardHeight || elevations.Any(height => height is < 0 or > 4096))
            throw new InvalidDataException("The stamped Tesla terrain asset has invalid elevations.");
        return elevations;
    }

    public ReservoirForcing StartExperiment(ExperimentDirection direction, bool applyToReference)
    {
        if (Scene != TerrainScene.Tesla || direction == ExperimentDirection.None || !Enum.IsDefined(direction))
            throw new InvalidOperationException("Select the Tesla scene and a flow direction.");
        Reset();
        ClearSprings();
        Direction = direction;
        for (var y = 57; y <= 70; y++)
        for (var x = 3; x <= Width - 4; x++)
            if ((x <= 9 || x >= Width - 10) && _terrainHeights[y * Width + x] == 0)
                _forcing[y * Width + x] = (x <= 9) == (direction == ExperimentDirection.Forward) ? 18 : 12;
        if (applyToReference) GetExperimentInitialWater().CopyTo(_waterDepths, 0);
        return GetForcing();
    }

    public float[] GetResetWater(bool cpuEnabled) => Direction != ExperimentDirection.None
        ? GetExperimentInitialWater() : cpuEnabled ? GetWaterSnapshot() : new float[_waterDepths.Length];

    private float[] GetExperimentInitialWater()
    {
        var water = new float[_terrainHeights.Length];
        for (var i = 0; i < water.Length; i++)
            water[i] = _forcing[i] >= 0 ? _forcing[i] : _terrainHeights[i] == 0 ? 15 : 0;
        return water;
    }

    public ReservoirForcing GetForcing() => new((float[])_forcing.Clone(), Direction);
    private void ApplyForcing()
    {
        if (Direction == ExperimentDirection.None) return;
        for (var i = 0; i < _forcing.Length; i++)
            if (_forcing[i] >= 0) _waterDepths[i] = _forcing[i];
    }

    public FlowProbe DischargeProbe => Probe(160, 63, 64);
    public FlowProbe BypassProbe => Probe(160, 57, 62, 65, 70);
    private FlowProbe Probe(int x, int top, int bottom) =>
        new(Enumerable.Range(top, bottom - top + 1).Select(y => 2 * (y * Width + x)).ToArray(),
            Enumerable.Repeat(1f / (12 * (Algorithm == FluidAlgorithm.Cellular ? 1 : TimeStepSeconds)),
                bottom - top + 1).ToArray());
    private FlowProbe Probe(int x, int topA, int bottomA, int topB, int bottomB)
    {
        var rows = Enumerable.Range(topA, bottomA - topA + 1).Concat(Enumerable.Range(topB, bottomB - topB + 1)).ToArray();
        return new(rows.Select(y => 2 * (y * Width + x)).ToArray(),
            Enumerable.Repeat(1f / (12 * (Algorithm == FluidAlgorithm.Cellular ? 1 : TimeStepSeconds)), rows.Length).ToArray());
    }

    public ExperimentReadout MeasureExperiment()
    {
        double Read(FlowProbe probe) => probe.Faces.Select((face, j) => (double)_transfers[face] * probe.Weights[j]).Sum();
        return new(Read(DischargeProbe), Read(BypassProbe));
    }
}

public enum FluidAlgorithm { Cellular, PipeFlux, TerrainHead, Mac }
public sealed record FluidState(float[] Water, float[] Momentum, int Cycles, FluidAlgorithm Algorithm)
{
    public FluidSettings Settings { get; init; } = new();
    public ExperimentDirection Direction { get; init; }
}
public readonly record struct FluidSummary(int Cycles, int WetCells, double TotalWater, float MaximumDepth);

public sealed record FluidCell(int X, int Y, int TerrainHeight, float WaterDepth);
