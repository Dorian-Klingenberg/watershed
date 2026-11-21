using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using WinRT;
using WinRT.Interop;
using WebGpuFluidSandbox.Simulation;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace WindowsD3D12FluidSandbox;

public sealed partial class MainWindow : Window
{
    private const uint WmMouseWheel = 0x020A;
    private static readonly UIntPtr WheelSubclassId = new(1);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint { public int X; public int Y; }

    private delegate IntPtr SubclassProc(IntPtr hWnd, uint message, IntPtr wParam,
        IntPtr lParam, UIntPtr subclassId, UIntPtr referenceData);

    [ComImport]
    [Guid("63aad0b8-7c24-40ff-85a8-640d944cc325")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface ISwapChainPanelNative
    {
        [PreserveSig]
        int SetSwapChain(IntPtr swapChain);
    }

    private sealed record TerrainMapFile(int Version, string Name, int Width, int Height,
        int[] ElevationInches, SpringSource[]? Springs = null);
    private sealed record ExperimentSample(int Tick, ExperimentReadout Readout);

    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly CellularFluidSimulation _simulation = CellularFluidSimulation.CreateDefault();
    private CellularFluidSimulation? _cpuReference;
    private ISwapChainPanelNative? _panelNative;
    private IntPtr _windowHandle;
    private SubclassProc? _wheelSubclassProc;
    private IntPtr _ownedSwapChain;
    private bool _initialized, _disposing, _running, _syncingControls, _cpuEnabled, _experimentModified;
    private string _terrainTool = "raise";
    private string? _cameraGesture;
    private bool _gestureDragged;
    private long _lastWheelTimestamp;
    private Windows.Foundation.Point _gestureStart, _lastStrokePoint;
    private Windows.Foundation.Point _cameraAnchorScreen = new(0.5, 0.5);
    private double _cameraAnchorX = 96, _cameraAnchorY = 64, _cameraAnchorZ;
    private double _gestureStartYaw, _gestureStartElevation, _gestureStartRoll;
    private uint _gestureHitX, _gestureHitY;
    private HashSet<int> _strokeCells = [];
    private (uint X, uint Y)? _lastStrokeHit;
    private int? _selectedIndex;
    private int _ticksPerDisplay = 2;
    private string _referenceStatus = "CPU reference off · D3D12 view active.";
    private string _mapStatus = "Raise terrain is selected.";
    private string _selectionText = "Select a cell in the field.";
    private string? _errorMessage;
    private ExperimentSample? _forwardSample, _reverseSample;
    private double _nativeStepMs;
    private double _renderMs;
    private double _ticksPerSecond;
    private uint _metricTicks;
    private long _metricStart = System.Diagnostics.Stopwatch.GetTimestamp();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int NativeInitialize(uint width, uint height, string shaderDirectory);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetSwapChain(out IntPtr swapChain);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetRenderSize(out uint width, out uint height);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeResize(uint width, uint height);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeReset();
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeDispose();
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetFrame([In] int[] terrain, [In] float[] water, [In] float[] springs, uint count, int resetSpan);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetCamera(float panX, float panY, float panZ, float zoom,
        float yawDegrees, float elevationDegrees, float rollDegrees, float anchorX, float anchorY);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetPreview([In] int[] cells, uint count);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetSelectedCell(int x, int y);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativePickCell(float normalizedX, float normalizedY, out uint x, out uint y, out float surfaceHeight);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativePickPoint(float normalizedX, float normalizedY, out float fieldX, out float fieldY, out float surfaceHeight);
    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int NativeGetInitializationReport([MarshalAs(UnmanagedType.LPStr)] StringBuilder buffer, uint bufferSize);
    [DllImport("comctl32.dll", CallingConvention = CallingConvention.Winapi, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowSubclass(IntPtr hWnd, SubclassProc callback, UIntPtr subclassId, UIntPtr referenceData);
    [DllImport("comctl32.dll", CallingConvention = CallingConvention.Winapi, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RemoveWindowSubclass(IntPtr hWnd, SubclassProc callback, UIntPtr subclassId);
    [DllImport("comctl32.dll", CallingConvention = CallingConvention.Winapi)]
    private static extern IntPtr DefSubclassProc(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CallingConvention = CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ScreenToClient(IntPtr hWnd, ref NativePoint point);

    public MainWindow()
    {
        InitializeComponent();
        RootGrid.AddHandler(UIElement.PointerWheelChangedEvent,
            new PointerEventHandler(FluidSurface_PointerWheelChanged), true);
        FluidSurface.AddHandler(UIElement.PointerWheelChangedEvent,
            new PointerEventHandler(FluidSurface_PointerWheelChanged), true);
        FluidSurface.Loaded += FluidSurface_Loaded;
        FluidSurface.SizeChanged += FluidSurface_SizeChanged;
        FluidSurface.CompositionScaleChanged += FluidSurface_CompositionScaleChanged;
        _timer.Tick += Timer_Tick;
        Closed += MainWindow_Closed;
        SyncSettingsControls();
        SyncCameraControls();
        UpdateControlVisibility();
        UpdateStatus();
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args) => DisposeNativeOnce();

    private void DisposeNativeOnce()
    {
        if (_disposing) return;
        _disposing = true;
        _running = false;
        _timer.Stop();
        if (_windowHandle != IntPtr.Zero && _wheelSubclassProc is not null)
        {
            try { RemoveWindowSubclass(_windowHandle, _wheelSubclassProc, WheelSubclassId); }
            catch { }
            _wheelSubclassProc = null;
            _windowHandle = IntPtr.Zero;
        }
        try { if (_initialized) NativeDispose(); }
        catch (Exception ex) { Console.WriteLine($"D3D12 disposal reported: {ex.Message}"); }
        _initialized = false;
        try { _panelNative?.SetSwapChain(IntPtr.Zero); }
        catch (Exception ex) { Console.WriteLine($"SwapChainPanel detach reported: {ex.Message}"); }
        _panelNative = null;
        if (_ownedSwapChain != IntPtr.Zero)
        {
            Marshal.Release(_ownedSwapChain);
            _ownedSwapChain = IntPtr.Zero;
        }
    }

    private void FluidSurface_Loaded(object sender, RoutedEventArgs e) => InitializeNativeSurface();

    private void FluidSurface_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!_initialized) return;
        var hr = NativeResize(PixelWidth(e.NewSize.Width), PixelHeight(e.NewSize.Height));
        if (hr == 0) { ApplyCamera(); UpdateEngineStatus(); }
        else SetError($"D3D12 resize failed: 0x{unchecked((uint)hr):X8}");
    }

    private void FluidSurface_CompositionScaleChanged(SwapChainPanel sender, object args)
    {
        if (!_initialized) return;
        var hr = NativeResize(PixelWidth(FluidSurface.ActualWidth), PixelHeight(FluidSurface.ActualHeight));
        if (hr == 0) { ApplyCamera(); UpdateEngineStatus(); }
        else SetError($"D3D12 DPI resize failed: 0x{unchecked((uint)hr):X8}");
    }

    private uint PixelWidth(double width) => Math.Max(1, (uint)Math.Ceiling(width * FluidSurface.CompositionScaleX));
    private uint PixelHeight(double height) => Math.Max(1, (uint)Math.Ceiling(height * FluidSurface.CompositionScaleY));

    private void InitializeNativeSurface()
    {
        if (_initialized || _disposing) return;
        try
        {
            var width = PixelWidth(FluidSurface.ActualWidth);
            var height = PixelHeight(FluidSurface.ActualHeight);
            var shaders = Path.Combine(AppContext.BaseDirectory, "native", "shaders");
            var hr = NativeInitialize(width, height, shaders);
            if (hr != 0) throw new InvalidOperationException($"NativeInitialize failed: 0x{unchecked((uint)hr):X8}");
            hr = NativeGetSwapChain(out var swapChain);
            if (hr != 0 || swapChain == IntPtr.Zero) throw new InvalidOperationException($"NativeGetSwapChain failed: 0x{unchecked((uint)hr):X8}");
            _ownedSwapChain = swapChain;
            _panelNative = (ISwapChainPanelNative)FluidSurface.As<ISwapChainPanelNative>();
            hr = _panelNative.SetSwapChain(swapChain);
            if (hr != 0) throw new InvalidOperationException($"SwapChainPanel handoff failed: 0x{unchecked((uint)hr):X8}");
            _windowHandle = WindowNative.GetWindowHandle(this);
            _wheelSubclassProc = WindowSubclassProcedure;
            if (!SetWindowSubclass(_windowHandle, _wheelSubclassProc, WheelSubclassId, UIntPtr.Zero))
                Console.WriteLine($"Mouse-wheel subclass installation failed: {Marshal.GetLastWin32Error()}");
            hr = NativeReset();
            if (hr != 0)
            {
                var report = new StringBuilder(4096);
                NativeGetInitializationReport(report, (uint)report.Capacity);
                throw new InvalidOperationException($"D3D12 first render failed: 0x{unchecked((uint)hr):X8} {report}");
            }
            _initialized = true;
            NativeSetSelectedCell(-1, -1);
            ResetCameraValues();
            UploadFrame(resetSpan: true);
            ApplyCamera();
            UpdateEngineStatus();
            UpdateStatus();
        }
        catch (Exception ex)
        {
            SetError($"D3D12 initialization failed: {ex.Message}");
            Console.WriteLine(ex);
        }
    }

    private void UploadFrame(bool resetSpan = false)
    {
        if (!_initialized) return;
        var start = System.Diagnostics.Stopwatch.GetTimestamp();
        var hr = NativeSetFrame(_simulation.GetTerrainSnapshot(), _simulation.GetWaterSnapshot(),
            _simulation.GetSpringRates(), (uint)(_simulation.Width * _simulation.Height), resetSpan ? 1 : 0);
        _renderMs = System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        if (hr != 0) throw new InvalidOperationException($"D3D12 frame upload failed: 0x{unchecked((uint)hr):X8}");
        if (_selectedIndex is int selected) NativeSetSelectedCell(selected % _simulation.Width, selected / _simulation.Width);
    }

    private void ApplyCamera()
    {
        if (!_initialized) return;
        try
        {
            var hr = NativeSetCamera((float)PanXSlider.Value, (float)PanYSlider.Value, (float)PanZSlider.Value,
                (float)ZoomSlider.Value, (float)YawSlider.Value, (float)ElevationSlider.Value, (float)RollSlider.Value,
                (float)_cameraAnchorScreen.X, (float)_cameraAnchorScreen.Y);
            if (hr != 0) SetError($"D3D12 camera update failed: 0x{unchecked((uint)hr):X8}");
        }
        catch (Exception ex) { SetError($"D3D12 camera update failed: {ex.Message}"); }
    }

    private void UpdateEngineStatus()
    {
        if (NativeGetRenderSize(out var width, out var height) == 0)
            EngineStatusText.Text = $"Direct3D 12 ready · {width}×{height}px";
        else
            EngineStatusText.Text = "Direct3D 12 rendering ready";
    }

    private void SyncCameraControls()
    {
        _syncingControls = true;
        try
        {
            PanXSlider.Value = Math.Clamp(_cameraAnchorX, PanXSlider.Minimum, PanXSlider.Maximum);
            PanYSlider.Value = Math.Clamp(_cameraAnchorY, PanYSlider.Minimum, PanYSlider.Maximum);
            PanZSlider.Value = Math.Clamp(_cameraAnchorZ, PanZSlider.Minimum, PanZSlider.Maximum);
        }
        finally { _syncingControls = false; }
    }

    private void ResetCameraValues()
    {
        var terrain = _simulation.GetTerrainSnapshot();
        _cameraAnchorX = _simulation.Width / 2.0;
        _cameraAnchorY = _simulation.Height / 2.0;
        _cameraAnchorZ = (terrain.Min() + terrain.Max()) * 0.35 / 2.0;
        _cameraAnchorScreen = new Windows.Foundation.Point(0.5, 0.5);
        _syncingControls = true;
        try
        {
            ZoomSlider.Value = 1;
            PanXSlider.Value = _cameraAnchorX;
            PanYSlider.Value = _cameraAnchorY;
            PanZSlider.Value = Math.Clamp(_cameraAnchorZ, PanZSlider.Minimum, PanZSlider.Maximum);
            YawSlider.Value = (360.0 - 0.6 * 180.0 / Math.PI) % 360.0;
            ElevationSlider.Value = 0.85 * 180.0 / Math.PI;
            RollSlider.Value = 0;
        }
        finally { _syncingControls = false; }
    }

    private void ViewSlider_ValueChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_initialized || _syncingControls) return;
        _cameraAnchorX = PanXSlider.Value;
        _cameraAnchorY = PanYSlider.Value;
        _cameraAnchorZ = PanZSlider.Value;
        ApplyCamera();
    }

    private void ResetViewButton_Click(object sender, RoutedEventArgs e)
    {
        ResetCameraValues();
        ApplyCamera();
    }

    private void FluidSurface_PointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized || _cameraGesture is not null) return;
        var point = e.GetCurrentPoint(FluidSurface);
        var position = point.Position;
        if (!TryPick(position, out var x, out var y, out _, out _, out _)) return;
        if (point.Properties.IsLeftButtonPressed) _cameraGesture = "orbit";
        else if (point.Properties.IsRightButtonPressed) _cameraGesture = "edit";
        else return;
        _gestureStart = _lastStrokePoint = position;
        _gestureStartYaw = YawSlider.Value;
        _gestureStartElevation = ElevationSlider.Value;
        _gestureStartRoll = RollSlider.Value;
        _gestureHitX = x;
        _gestureHitY = y;
        _gestureDragged = false;
        _lastStrokeHit = (x, y);
        _strokeCells = [checked((int)(y * _simulation.Width + x))];
        EngineStatusText.Text = $"{_cameraGesture} started · cell ({x}, {y})";
        FluidSurface.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void FluidSurface_PointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized) return;
        var point = e.GetCurrentPoint(FluidSurface);
        if (_cameraGesture == "orbit")
        {
            var dx = point.Position.X - _gestureStart.X;
            var dy = point.Position.Y - _gestureStart.Y;
            if (!_gestureDragged && Distance(point.Position, _gestureStart) > 5.0)
            {
                _gestureDragged = true;
                if (TryGetAnchorAt(_gestureStart, out var x, out var y, out var z))
                {
                    _cameraAnchorX = x;
                    _cameraAnchorY = y;
                    _cameraAnchorZ = z;
                    _cameraAnchorScreen = NormalizeSurfacePoint(_gestureStart);
                }
            }
            if (_gestureDragged)
            {
                _syncingControls = true;
                try
                {
                    YawSlider.Value = (_gestureStartYaw + dx * 0.008 * 180.0 / Math.PI + 1440.0) % 360.0;
                    ElevationSlider.Value = Math.Clamp(_gestureStartElevation + dy * 0.006 * 180.0 / Math.PI,
                        ElevationSlider.Minimum, ElevationSlider.Maximum);
                    RollSlider.Value = _gestureStartRoll;
                    PanXSlider.Value = Math.Clamp(_cameraAnchorX, PanXSlider.Minimum, PanXSlider.Maximum);
                    PanYSlider.Value = Math.Clamp(_cameraAnchorY, PanYSlider.Minimum, PanYSlider.Maximum);
                    PanZSlider.Value = Math.Clamp(_cameraAnchorZ, PanZSlider.Minimum, PanZSlider.Maximum);
                }
                finally { _syncingControls = false; }
                ApplyCamera();
                EngineStatusText.Text = $"Orbit · yaw {YawSlider.Value:F1}° · elevation {ElevationSlider.Value:F1}°";
            }
            e.Handled = true;
            return;
        }
        if (_cameraGesture == "edit")
        {
            if (!_gestureDragged && Distance(point.Position, _gestureStart) > 5.0) _gestureDragged = true;
            if (_gestureDragged) SampleStroke(_lastStrokePoint, point.Position);
            _lastStrokePoint = point.Position;
            e.Handled = true;
            return;
        }
        if (TryPick(point.Position, out var hoverX, out var hoverY, out _, out _, out _))
            NativeSetSelectedCell((int)hoverX, (int)hoverY);
    }

    private void SampleStroke(Windows.Foundation.Point from, Windows.Foundation.Point to)
    {
        var distance = Distance(from, to);
        var steps = Math.Clamp((int)Math.Ceiling(distance / Math.Max(2.0, CameraScale() / 2.0)), 1, 32);
        for (var i = 1; i <= steps; i++)
        {
            var t = (double)i / steps;
            var p = new Windows.Foundation.Point(from.X + (to.X - from.X) * t, from.Y + (to.Y - from.Y) * t);
            if (!TryPick(p, out var x, out var y, out _, out _, out _))
            {
                _lastStrokeHit = null;
                continue;
            }
            if (_lastStrokeHit is { } last) AddStrokeLine(last.X, last.Y, x, y);
            else _strokeCells.Add(checked((int)(y * _simulation.Width + x)));
            _lastStrokeHit = (x, y);
        }
        var ids = _strokeCells.Order().ToArray();
        NativeSetPreview(ids, (uint)ids.Length);
    }

    private double CameraScale()
    {
        var terrain = _simulation.GetTerrainSnapshot();
        var span = Math.Max(_simulation.Width, _simulation.Height) + (terrain.Max() - terrain.Min()) * 0.35;
        return Math.Min(FluidSurface.ActualWidth, FluidSurface.ActualHeight * 1.45) * 0.88 /
            Math.Max(1.0, span) * ZoomSlider.Value;
    }

    private void AddStrokeLine(uint fromX, uint fromY, uint toX, uint toY)
    {
        var steps = Math.Max(Math.Abs((int)toX - (int)fromX), Math.Abs((int)toY - (int)fromY));
        for (var i = 0; i <= Math.Max(steps, 1); i++)
        {
            var t = (double)i / Math.Max(steps, 1);
            var x = (uint)Math.Clamp((int)Math.Round(fromX + ((int)toX - (int)fromX) * t), 0, _simulation.Width - 1);
            var y = (uint)Math.Clamp((int)Math.Round(fromY + ((int)toY - (int)fromY) * t), 0, _simulation.Height - 1);
            _strokeCells.Add(checked((int)(y * _simulation.Width + x)));
        }
    }

    private void FluidSurface_PointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (_cameraGesture is null) return;
        // Apply the release point too: the final native pointer position is not
        // guaranteed to arrive in a separate PointerMoved event.
        FluidSurface_PointerMoved(sender, e);
        var gesture = _cameraGesture;
        _cameraGesture = null;
        FluidSurface.ReleasePointerCapture(e.Pointer);
        if (gesture == "orbit")
        {
            if (!_gestureDragged && TryPick(_gestureStart, out var x, out var y, out _, out _, out _)) SelectCell(x, y);
        }
        else if (_gestureDragged) CommitTerrainStroke();
        else AddSpring(_gestureHitX, _gestureHitY);
        if (gesture == "orbit" && _gestureDragged)
            EngineStatusText.Text = $"Camera · yaw {YawSlider.Value:F1}° · elevation {ElevationSlider.Value:F1}° · roll {RollSlider.Value:F1}°";
        else if (gesture == "orbit") SelectCell(_gestureHitX, _gestureHitY);
        _strokeCells.Clear();
        _lastStrokeHit = null;
        NativeSetPreview([], 0);
        e.Handled = true;
    }

    private void CommitTerrainStroke()
    {
        if (_strokeCells.Count == 0) return;
        var cells = _strokeCells.Order().ToArray();
        if (_terrainTool == "carve")
        {
            _simulation.CarveChannel(cells);
            _cpuReference?.CarveChannel(cells);
            TerrainStatusText.Text = $"Carved 12 in into {_strokeCells.Count} cells.";
        }
        else
        {
            _simulation.AddBarrier(cells);
            _cpuReference?.AddBarrier(cells);
            TerrainStatusText.Text = $"Raised {_strokeCells.Count} cells by 12 in.";
        }
        MarkManualExperimentEdit();
        ClearComparison();
        UploadFrame();
        UpdateStatus();
    }

    private void AddSpring(uint x, uint y)
    {
        try
        {
            var rate = _simulation.AddSpring((int)x, (int)y);
            _cpuReference?.AddSpring((int)x, (int)y);
            _selectedIndex = checked((int)(y * _simulation.Width + x));
            TerrainStatusText.Text = $"Spring at ({x}, {y}) · {rate:F2} ft³/s.";
            MarkManualExperimentEdit();
            ClearComparison();
            UploadFrame();
            UpdateStatus();
        }
        catch (Exception ex) { SetError(ex.Message); }
    }

    private void FluidSurface_PointerCanceled(object sender, PointerRoutedEventArgs e) => CancelGesture(e.Pointer);
    private void FluidSurface_PointerCaptureLost(object sender, PointerRoutedEventArgs e) => CancelGesture(e.Pointer);

    private void CancelGesture(Microsoft.UI.Xaml.Input.Pointer pointer)
    {
        if (_cameraGesture is null) return;
        _cameraGesture = null;
        _gestureDragged = false;
        _strokeCells.Clear();
        _lastStrokeHit = null;
        try { NativeSetPreview([], 0); } catch { }
    }

    private void FluidSurface_PointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized || _cameraGesture is not null) return;
        var now = Environment.TickCount64;
        if (now - _lastWheelTimestamp < 25) return;
        _lastWheelTimestamp = now;
        if (!HandleWheelAt(e.GetCurrentPoint(FluidSurface).Position, e.GetCurrentPoint(FluidSurface).Properties.MouseWheelDelta)) return;
        e.Handled = true;
    }

    private bool HandleWheelAt(Windows.Foundation.Point position, int delta)
    {
        if (!_initialized || _cameraGesture is not null) return false;
        if (!TryGetAnchorAt(position, out var x, out var y, out var z)) return false;
        var pixelDelta = delta / 120.0 * 48.0;
        _cameraAnchorX = x; _cameraAnchorY = y; _cameraAnchorZ = z;
        _cameraAnchorScreen = NormalizeSurfacePoint(position);
        _syncingControls = true;
        try
        {
            ZoomSlider.Value = Math.Clamp(ZoomSlider.Value * Math.Exp(pixelDelta * 0.0015), ZoomSlider.Minimum, ZoomSlider.Maximum);
            PanXSlider.Value = Math.Clamp(x, PanXSlider.Minimum, PanXSlider.Maximum);
            PanYSlider.Value = Math.Clamp(y, PanYSlider.Minimum, PanYSlider.Maximum);
            PanZSlider.Value = Math.Clamp(z, PanZSlider.Minimum, PanZSlider.Maximum);
        }
        finally { _syncingControls = false; }
        ApplyCamera();
        EngineStatusText.Text = $"Zoom {ZoomSlider.Value:F2}× · pivot ({x:F1}, {y:F1}, {z:F1})";
        return true;
    }

    private IntPtr WindowSubclassProcedure(IntPtr hWnd, uint message, IntPtr wParam,
        IntPtr lParam, UIntPtr subclassId, UIntPtr referenceData)
    {
        try
        {
            if (message == WmMouseWheel && _initialized && _cameraGesture is null)
            {
                var delta = unchecked((short)(wParam.ToInt64() >> 16));
                var raw = lParam.ToInt64();
                var screenPoint = new NativePoint { X = unchecked((short)(raw & 0xffff)), Y = unchecked((short)((raw >> 16) & 0xffff)) };
                if (ScreenToClient(hWnd, ref screenPoint))
                {
                    var origin = FluidSurface.TransformToVisual(RootGrid).TransformPoint(new Windows.Foundation.Point(0, 0));
                    var position = new Windows.Foundation.Point(
                        screenPoint.X / Math.Max(0.1, FluidSurface.CompositionScaleX) - origin.X,
                        screenPoint.Y / Math.Max(0.1, FluidSurface.CompositionScaleY) - origin.Y);
                    if (position.X >= 0 && position.Y >= 0 && position.X <= FluidSurface.ActualWidth &&
                        position.Y <= FluidSurface.ActualHeight && HandleWheelAt(position, delta))
                        return IntPtr.Zero;
                }
            }
        }
        catch (Exception ex) { SetError($"Mouse-wheel input failed: {ex.Message}"); }
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    private bool TryGetAnchorAt(Windows.Foundation.Point position, out double x, out double y, out double z)
    {
        try
        {
            var point = NormalizeSurfacePoint(position);
            var hr = NativePickPoint((float)point.X, (float)point.Y, out var fieldX, out var fieldY, out var fieldZ);
            x = fieldX; y = fieldY; z = fieldZ;
            return hr == 0;
        }
        catch (Exception ex)
        {
            x = y = z = 0;
            SetError($"D3D12 picking failed: {ex.Message}");
            return false;
        }
    }

    private bool TryPick(Windows.Foundation.Point position, out uint x, out uint y,
        out float fieldX, out float fieldY, out float fieldZ)
    {
        try
        {
            var point = NormalizeSurfacePoint(position);
            var hr = NativePickCell((float)point.X, (float)point.Y, out x, out y, out _);
            if (hr != 0) { fieldX = fieldY = fieldZ = 0; return false; }
            hr = NativePickPoint((float)point.X, (float)point.Y, out fieldX, out fieldY, out fieldZ);
            return hr == 0;
        }
        catch (Exception ex)
        {
            x = y = 0; fieldX = fieldY = fieldZ = 0;
            SetError($"D3D12 picking failed: {ex.Message}");
            return false;
        }
    }

    private Windows.Foundation.Point NormalizeSurfacePoint(Windows.Foundation.Point p) => new(
        Math.Clamp(p.X / Math.Max(1, FluidSurface.ActualWidth), 0, 1),
        Math.Clamp(p.Y / Math.Max(1, FluidSurface.ActualHeight), 0, 1));

    private static double Distance(Windows.Foundation.Point a, Windows.Foundation.Point b)
    {
        var dx = a.X - b.X; var dy = a.Y - b.Y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    private void SelectCell(uint x, uint y)
    {
        _selectedIndex = checked((int)(y * _simulation.Width + x));
        NativeSetSelectedCell((int)x, (int)y);
        var cell = _simulation.GetCell((int)x, (int)y);
        var spring = _simulation.GetSpringRate((int)x, (int)y);
        _selectionText = $"Cell ({x}, {y}) · terrain {cell.TerrainHeight} in";
        if (spring > 0) _selectionText += $" · spring {spring:F2} ft³/s";
        _selectionText += $" · D3D12 view water {cell.WaterDepth:F3} in";
        if (_cpuReference is not null) _selectionText += $" · CPU water {_cpuReference.GetCell((int)x, (int)y).WaterDepth:F3} in";
        SelectionStatusText.Text = _selectionText;
        EngineStatusText.Text = $"Selected cell ({x}, {y})";
        RemoveSpringButton.IsEnabled = spring > 0;
    }

    private void RunButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized) return;
        _running = !_running;
        RunButton.Content = _running ? "Pause" : "Run";
        if (_running) _timer.Start(); else _timer.Stop();
    }

    private void Timer_Tick(object? sender, object e)
    {
        if (!_running || !_initialized) return;
        try { AdvanceTicks(_ticksPerDisplay); }
        catch (Exception ex) { StopRun(); SetError(ex.Message); }
    }

    private void AdvanceTicks(int count)
    {
        var start = System.Diagnostics.Stopwatch.GetTimestamp();
        for (var i = 0; i < count; i++)
        {
            _simulation.Step();
            _cpuReference?.Step();
            if (_simulation.Direction != ExperimentDirection.None && !_experimentModified)
                CaptureExperimentSample();
        }
        _nativeStepMs = System.Diagnostics.Stopwatch.GetElapsedTime(start).TotalMilliseconds / count;
        _metricTicks += (uint)count;
        UploadFrame();
        UpdateStatus();
    }

    private void StepButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized) return;
        try { AdvanceTicks(1); } catch (Exception ex) { SetError(ex.Message); }
    }

    private void PourButton_Click(object sender, RoutedEventArgs e)
    {
        var point = _simulation.PourLocation;
        var radius = _simulation.Scene == TerrainScene.Tesla ? 0 : 4;
        _simulation.AddWater(point.X, point.Y, radius, 12);
        _cpuReference?.AddWater(point.X, point.Y, radius, 12);
        MarkManualExperimentEdit();
        ClearComparison();
        UploadFrame();
        UpdateStatus();
    }

    private void ResetButton_Click(object sender, RoutedEventArgs e)
    {
        StopRun();
        _simulation.Reset();
        _cpuReference?.Reset();
        _forwardSample = _reverseSample = null;
        _experimentModified = false;
        ClearComparison();
        UploadFrame(resetSpan: true);
        UpdateStatus();
    }

    private void TicksPerDisplayBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TicksPerDisplayBox.SelectedItem is ComboBoxItem item && int.TryParse(item.Tag?.ToString(), out var ticks))
            _ticksPerDisplay = ticks;
    }

    private void CpuReferenceToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_initialized) return;
        _cpuEnabled = CpuReferenceToggle.IsOn;
        _cpuReference = _cpuEnabled ? _simulation.Clone() : null;
        _referenceStatus = _cpuEnabled
            ? $"CPU reference synchronized at tick {_simulation.CycleCount} · new comparison baseline."
            : "CPU reference off · Direct3D 12 presentation active.";
        ReferenceStatusText.Text = _referenceStatus;
        ClearComparison();
        UpdateStatus();
    }

    private void CompareButton_Click(object sender, RoutedEventArgs e)
    {
        if (_cpuReference is null) return;
        try
        {
            var result = FluidComparison.Compare(_cpuReference.GetStateSnapshot(), _simulation.GetStateSnapshot());
            ReferenceStatusText.Text = result.Matches
                ? $"Primary ↔ CPU reference state matches · {result.CellsCompared} depths, {result.MomentumValuesCompared} face values."
                : $"Primary ↔ CPU reference mismatch · {result.MismatchedCells} depths, {result.MismatchedMomentumValues} face values.";
        }
        catch (Exception ex) { SetError($"Comparison failed: {ex.Message}"); }
    }

    private void AlgorithmBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_initialized || AlgorithmBox.SelectedItem is not ComboBoxItem item || !int.TryParse(item.Tag?.ToString(), out var value)) return;
        StopRun();
        var algorithm = (FluidAlgorithm)value;
        _simulation.SelectAlgorithm(algorithm);
        _cpuReference?.SelectAlgorithm(algorithm);
        _forwardSample = _reverseSample = null;
        _experimentModified = false;
        ClearComparison();
        UpdateControlVisibility();
        UploadFrame();
        UpdateStatus();
    }

    private void SceneBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_initialized || SceneBox.SelectedItem is not ComboBoxItem item || !int.TryParse(item.Tag?.ToString(), out var value)) return;
        StopRun();
        _simulation.SelectScene((TerrainScene)value);
        _cpuReference?.SelectScene((TerrainScene)value);
        _forwardSample = _reverseSample = null;
        _experimentModified = false;
        ClearComparison();
        UpdateControlVisibility();
        _mapStatus = $"{_simulation.Scene} selected · paused · water and ticks cleared; camera preserved.";
        TerrainStatusText.Text = _mapStatus;
        UploadFrame(resetSpan: true);
        UpdateStatus();
    }

    private void TerrainToolBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TerrainToolBox.SelectedItem is ComboBoxItem item)
        {
            _terrainTool = item.Tag?.ToString() == "carve" ? "carve" : "raise";
            if (_initialized)
                TerrainStatusText.Text = _terrainTool == "carve" ? "Carve channel is selected." : "Raise terrain is selected.";
        }
    }

    private void FlowSettings_ValueChanged(NumberBox sender, NumberBoxValueChangedEventArgs args)
    {
        if (!_initialized || _syncingControls) return;
        try
        {
            var settings = new FluidSettings(
                (float)CellularMaxFlowBox.Value, (float)CellularSettleRateBox.Value,
                (float)PipeAreaBox.Value, (float)PipeRetentionBox.Value,
                (float)TerrainHeadAreaBox.Value, (float)ManningRoughnessBox.Value,
                (float)MacDampingBox.Value, MacAdvectionBox.IsChecked == true);
            _simulation.Configure(settings);
            _cpuReference?.Configure(settings);
            ClearComparison();
            UploadFrame();
            UpdateStatus();
        }
        catch (Exception ex) { SetError(ex.Message); }
    }

    private void MacAdvectionBox_Changed(object sender, RoutedEventArgs e)
    {
        if (_initialized && !_syncingControls && MacAdvectionBox.IsChecked is not null)
            FlowSettings_ValueChanged(CellularMaxFlowBox, null!);
    }

    private void DefaultsButton_Click(object sender, RoutedEventArgs e)
    {
        _syncingControls = true;
        try
        {
            CellularMaxFlowBox.Value = 2; CellularSettleRateBox.Value = 0.5;
            PipeAreaBox.Value = 0.25; PipeRetentionBox.Value = 0.94;
            TerrainHeadAreaBox.Value = 0.25; ManningRoughnessBox.Value = 0.04;
            MacDampingBox.Value = 0.02; MacAdvectionBox.IsChecked = false;
        }
        finally { _syncingControls = false; }
        FlowSettings_ValueChanged(CellularMaxFlowBox, null!);
    }

    private void SyncSettingsControls()
    {
        _syncingControls = true;
        try
        {
            var s = _simulation.Settings;
            CellularMaxFlowBox.Value = s.CellularMaxFlow; CellularSettleRateBox.Value = s.CellularSettleRate;
            PipeAreaBox.Value = s.PipeArea; PipeRetentionBox.Value = s.PipeRetention;
            TerrainHeadAreaBox.Value = s.TerrainHeadArea; ManningRoughnessBox.Value = s.ManningRoughness;
            MacDampingBox.Value = s.MacDamping; MacAdvectionBox.IsChecked = s.MacAdvection;
        }
        finally { _syncingControls = false; }
    }

    private void UpdateControlVisibility()
    {
        var algorithm = _simulation.Algorithm;
        CellularMaxFlowBox.Visibility = algorithm == FluidAlgorithm.Cellular ? Visibility.Visible : Visibility.Collapsed;
        CellularSettleRateBox.Visibility = algorithm == FluidAlgorithm.Cellular ? Visibility.Visible : Visibility.Collapsed;
        PipeAreaBox.Visibility = algorithm == FluidAlgorithm.PipeFlux ? Visibility.Visible : Visibility.Collapsed;
        PipeRetentionBox.Visibility = algorithm == FluidAlgorithm.PipeFlux ? Visibility.Visible : Visibility.Collapsed;
        TerrainHeadAreaBox.Visibility = algorithm == FluidAlgorithm.TerrainHead ? Visibility.Visible : Visibility.Collapsed;
        ManningRoughnessBox.Visibility = algorithm == FluidAlgorithm.TerrainHead ? Visibility.Visible : Visibility.Collapsed;
        MacDampingBox.Visibility = algorithm == FluidAlgorithm.Mac ? Visibility.Visible : Visibility.Collapsed;
        MacAdvectionBox.Visibility = algorithm == FluidAlgorithm.Mac ? Visibility.Visible : Visibility.Collapsed;
        ExperimentPanel.Visibility = _simulation.Scene == TerrainScene.Tesla ? Visibility.Visible : Visibility.Collapsed;
        CompareButton.IsEnabled = _cpuReference is not null;
    }

    private void SyncSettingsForAlgorithm()
    {
        _syncingControls = true;
        try
        {
            var s = _simulation.Settings;
            CellularMaxFlowBox.Value = s.CellularMaxFlow; CellularSettleRateBox.Value = s.CellularSettleRate;
            PipeAreaBox.Value = s.PipeArea; PipeRetentionBox.Value = s.PipeRetention;
            TerrainHeadAreaBox.Value = s.TerrainHeadArea; ManningRoughnessBox.Value = s.ManningRoughness;
            MacDampingBox.Value = s.MacDamping; MacAdvectionBox.IsChecked = s.MacAdvection;
        }
        finally { _syncingControls = false; }
    }

    private void ForwardExperimentButton_Click(object sender, RoutedEventArgs e) => StartExperiment(ExperimentDirection.Forward);
    private void ReverseExperimentButton_Click(object sender, RoutedEventArgs e) => StartExperiment(ExperimentDirection.Reverse);

    private void StartExperiment(ExperimentDirection direction)
    {
        try
        {
            StopRun();
            _simulation.StartExperiment(direction, true);
            _cpuReference?.StartExperiment(direction, true);
            _forwardSample = _reverseSample = null;
            _experimentModified = false;
            ClearComparison();
            UpdateExperimentReadout();
            UploadFrame();
            UpdateStatus();
        }
        catch (Exception ex) { SetError(ex.Message); }
    }

    private void AdvanceExperimentButton_Click(object sender, RoutedEventArgs e)
    {
        if (_simulation.Direction == ExperimentDirection.None) return;
        try { AdvanceTicks(128); } catch (Exception ex) { SetError(ex.Message); }
    }

    private void CaptureExperimentSample()
    {
        var sample = new ExperimentSample(_simulation.CycleCount, _simulation.MeasureExperiment());
        if (_simulation.Direction == ExperimentDirection.Forward) _forwardSample = sample;
        else if (_simulation.Direction == ExperimentDirection.Reverse) _reverseSample = sample;
        UpdateExperimentReadout();
    }

    private void UpdateExperimentReadout()
    {
        var reading = _simulation.MeasureExperiment();
        var unit = _simulation.Algorithm == FluidAlgorithm.Cellular ? "ft³/tick" : "ft³/s";
        ExperimentStatusText.Text = $"{_simulation.Direction} · {reading.Discharge:F5} {unit} · bypass {reading.BypassFlow:F5} {unit}";
        if (_forwardSample is { } forward && _reverseSample is { } reverse && forward.Tick == reverse.Tick)
            ExperimentComparisonText.Text = $"Forward {forward.Readout.Discharge:F5} at {forward.Tick} · reverse {reverse.Readout.Discharge:F5} at {reverse.Tick} · |difference| {Math.Abs(Math.Abs(forward.Readout.Discharge) - Math.Abs(reverse.Readout.Discharge)):F5} {unit}";
        else
            ExperimentComparisonText.Text = _experimentModified ? "Manual edits applied · restart an experiment to reset." : string.Empty;
    }

    private void RemoveSpringButton_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedIndex is not int index) return;
        var x = index % _simulation.Width; var y = index / _simulation.Width;
        _simulation.RemoveSpring(x, y);
        _cpuReference?.RemoveSpring(x, y);
        ClearComparison(); UploadFrame(); UpdateStatus();
    }

    private void ClearSpringsButton_Click(object sender, RoutedEventArgs e)
    {
        _simulation.ClearSprings(); _cpuReference?.ClearSprings();
        ClearComparison(); UploadFrame(); UpdateStatus();
    }

    private async void SaveTerrainButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var json = SerializeTerrainMap();
            var localPath = SavedMapPath();
            Directory.CreateDirectory(Path.GetDirectoryName(localPath)!);
            await File.WriteAllTextAsync(localPath, json);
            var picker = new FileSavePicker
            {
                SuggestedStartLocation = PickerLocationId.DocumentsLibrary,
                SuggestedFileName = "terrain-and-springs"
            };
            picker.FileTypeChoices.Add("JSON terrain map", new List<string> { ".json" });
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            var file = await picker.PickSaveFileAsync();
            if (file is not null) await FileIO.WriteTextAsync(file, json);
            TerrainStatusText.Text = file is null ? "Map saved for Restore." : $"Map saved and exported: {file.Name}.";
        }
        catch (Exception ex) { SetError($"Map save failed: {ex.Message}"); }
    }

    private async void RestoreTerrainButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var path = SavedMapPath();
            if (!File.Exists(path)) { TerrainStatusText.Text = "No saved map is available yet."; return; }
            await LoadTerrainMapAsync(await File.ReadAllTextAsync(path));
            TerrainStatusText.Text = "Saved terrain and springs restored · fluid cleared · camera preserved.";
        }
        catch (Exception ex) { SetError($"Map restore failed: {ex.Message}"); }
    }

    private async void ImportTerrainButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
            picker.FileTypeFilter.Add(".json");
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
            var file = await picker.PickSingleFileAsync();
            if (file is null) return;
            await LoadTerrainMapAsync(await FileIO.ReadTextAsync(file));
            TerrainStatusText.Text = $"Loaded {file.Name} · terrain and springs restored; fluid cleared.";
        }
        catch (Exception ex) { SetError($"Map import failed: {ex.Message}"); }
    }

    private string SerializeTerrainMap() => JsonSerializer.Serialize(
        new TerrainMapFile(2, "terrain-and-springs", _simulation.Width, _simulation.Height,
            _simulation.GetTerrainSnapshot(), _simulation.GetSpringSnapshot()), new JsonSerializerOptions { WriteIndented = true });

    private async Task LoadTerrainMapAsync(string json)
    {
        var map = JsonSerializer.Deserialize<TerrainMapFile>(json)
            ?? throw new InvalidDataException("The file is not a terrain map.");
        if (map.Version is not (1 or 2) || map.Width != _simulation.Width || map.Height != _simulation.Height || map.ElevationInches is null)
            throw new InvalidDataException("Map version or dimensions are not supported.");
        StopRun();
        _simulation.LoadTerrainSnapshot(map.ElevationInches, map.Version == 2 ? map.Springs : null);
        _cpuReference = _cpuEnabled ? _simulation.Clone() : null;
        ClearComparison();
        UploadFrame(resetSpan: true);
        UpdateStatus();
        await Task.CompletedTask;
    }

    private string SavedMapPath() => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "D3D12FluidSandbox", "saved-terrain.json");

    private void StopRun()
    {
        _running = false;
        _timer.Stop();
        RunButton.Content = "Run";
    }

    private void MarkManualExperimentEdit()
    {
        if (_simulation.Direction == ExperimentDirection.None) return;
        _experimentModified = true;
        UpdateExperimentReadout();
    }

    private void ClearComparison()
    {
        if (_cpuReference is not null) ReferenceStatusText.Text = _referenceStatus;
    }

    private void UpdateStatus()
    {
        var summary = _simulation.GetSummary();
        var elapsed = System.Diagnostics.Stopwatch.GetElapsedTime(_metricStart).TotalSeconds;
        if (elapsed >= 0.5)
        {
            _ticksPerSecond = _metricTicks / elapsed;
            _metricTicks = 0;
            _metricStart = System.Diagnostics.Stopwatch.GetTimestamp();
        }
        var springs = _simulation.GetSpringSnapshot();
        SpringReadoutText.Text = $"Springs: {springs.Length} · {springs.Sum(s => s.FlowCubicFeetPerSecond):F2} ft³/s";
        RemoveSpringButton.IsEnabled = _selectedIndex is int selected &&
            _simulation.GetSpringRate(selected % _simulation.Width, selected / _simulation.Width) > 0;
        ClearSpringsButton.IsEnabled = springs.Length > 0;
        CompareButton.IsEnabled = _cpuReference is not null;
        ReferenceStatusText.Text = _referenceStatus;
        if (_selectedIndex is int index)
        {
            var cell = _simulation.GetCell(index % _simulation.Width, index / _simulation.Width);
            var spring = _simulation.GetSpringRate(cell.X, cell.Y);
            _selectionText = $"Cell ({cell.X}, {cell.Y}) · terrain {cell.TerrainHeight} in";
            if (spring > 0) _selectionText += $" · spring {spring:F2} ft³/s";
            _selectionText += $" · D3D12 view water {cell.WaterDepth:F3} in";
            if (_cpuReference is not null) _selectionText += $" · CPU water {_cpuReference.GetCell(cell.X, cell.Y).WaterDepth:F3} in";
            SelectionStatusText.Text = _selectionText;
        }
        StatusText.Text = $"Tick {summary.Cycles} · wet cells {summary.WetCells} · water {summary.TotalWater:F1} cell-in · max depth {summary.MaximumDepth:F2} in · solver {_nativeStepMs:F2} ms/tick";
        TickSummaryText.Text = $"TICK {summary.Cycles} · {(_cpuReference is null ? "D3D12 view + shared solver" : "D3D12 view + CPU reference")}";
        PerformanceText.Text = $"TICKS / SEC {_ticksPerSecond:F1} · SOLVER {_nativeStepMs:F2} ms/tick · D3D12 DRAW/UPLOAD {_renderMs:F2} ms";
        if (_simulation.Direction != ExperimentDirection.None) UpdateExperimentReadout();
    }

    private void SetError(string message)
    {
        _errorMessage = message;
        StatusText.Text = message;
        Console.WriteLine(message);
    }
}
