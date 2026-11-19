using System.Runtime.InteropServices;
using System.Text;

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using WinRT;

namespace WindowsD3D12FluidSandbox;

public sealed partial class MainWindow : Window
{
    [ComImport]
    [Guid("63aad0b8-7c24-40ff-85a8-640d944cc325")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface ISwapChainPanelNative
    {
        [PreserveSig]
        int SetSwapChain(IntPtr swapChain);
    }

    private readonly DispatcherTimer _timer = new()
    {
        Interval = TimeSpan.FromMilliseconds(16)
    };

    private bool _running;
    private bool _initialized;
    private bool _disposing;
    private bool _cpuReference;
    private uint _batchSize = 1;
    private string _terrainTool = "raise";
    private string? _cameraGesture;
    private Windows.Foundation.Point _cameraGestureStart;
    private double _cameraGestureStartPanX;
    private double _cameraGestureStartPanY;
    private double _cameraGestureStartYaw;
    private double _cameraGestureStartElevation;
    private double _orbitAnchorX;
    private double _orbitAnchorY;
    private bool _rightDragOccurred;
    private IntPtr _ownedSwapChain;
    private ISwapChainPanelNative? _panelNative;

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeStatus
    {
        public uint Tick;
        public uint WetCells;
        public float TotalWater;
        public float MaximumDepth;
        public uint CpuTick;
        public uint CpuWetCells;
        public float CpuTotalWater;
        public float CpuMaximumDepth;
        public uint MismatchCells;
        public float MaximumDepthMismatch;
        public uint BatchSize;
        public uint CpuReferenceEnabled;
        public float NativeStepMsPerTick;
        public float CpuStepMsPerTick;
        public float DrawMs;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeCellStatus
    {
        public uint X;
        public uint Y;
        public int Terrain;
        public float Water;
        public float CpuWater;
    }

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int NativeInitialize(uint width, uint height, string shaderDirectory);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetSwapChain(out IntPtr swapChain);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeResize(uint width, uint height);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeReset();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativePour(float inches);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeStep();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeStepBatch(uint ticks);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativePourAt(float normalizedX, float normalizedY, float inches);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeEditTerrain(float normalizedX, float normalizedY, int raiseTerrain);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetCpuReference(int enabled);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetView(float panX, float panY, float zoom, uint mode);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetOrbit(float yawDegrees, float elevationDegrees);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetSettings(float maxFlow, float settleRate);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeSetSelection(float normalizedX, float normalizedY);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetCellStatus(uint x, uint y, out NativeCellStatus status);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetStatus(out NativeStatus status);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeDispose();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetDeviceRemovedReason();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int NativeGetInitializationReport(
        [MarshalAs(UnmanagedType.LPStr)] StringBuilder buffer,
        uint bufferSize);

    public MainWindow()
    {
        InitializeComponent();

        FluidSurface.Loaded += FluidSurface_Loaded;
        FluidSurface.SizeChanged += FluidSurface_SizeChanged;
        _timer.Tick += Timer_Tick;
        Closed += MainWindow_Closed;
        AppWindow.Closing += MainWindow_AppWindowClosing;
    }

    private void MainWindow_AppWindowClosing(Microsoft.UI.Windowing.AppWindow sender,
        Microsoft.UI.Windowing.AppWindowClosingEventArgs args)
    {
        DisposeNativeOnce();
    }

    private void MainWindow_Closed(object sender, WindowEventArgs args)
    {
        DisposeNativeOnce();
    }

    private void DisposeNativeOnce()
    {
        if (_disposing)
            return;
        _disposing = true;
        _running = false;
        _timer.Stop();

        if (_panelNative != null)
        {
            try
            {
                _panelNative.SetSwapChain(IntPtr.Zero);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"D3D12 SwapChainPanel detach failed: {ex.Message}");
            }
            _panelNative = null;
        }

        if (_ownedSwapChain != IntPtr.Zero)
        {
            Marshal.Release(_ownedSwapChain);
            _ownedSwapChain = IntPtr.Zero;
        }

        if (_initialized)
        {
            var hr = NativeDispose();
            _initialized = false;
            if (hr != 0)
                Console.WriteLine($"D3D12 disposal failed: 0x{unchecked((uint)hr):X8}");
        }
    }

    private void FluidSurface_Loaded(object sender, RoutedEventArgs e)
    {
        InitializeNativeSurface();
    }

    private void FluidSurface_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!_initialized)
            return;

        var width = Math.Max(1, (uint)e.NewSize.Width);
        var height = Math.Max(1, (uint)e.NewSize.Height);
        var hr = NativeResize(width, height);
        if (hr != 0)
            StatusText.Text = $"D3D12 resize failed: 0x{unchecked((uint)hr):X8}";
    }

    private void InitializeNativeSurface()
    {
        try
        {
            var width = Math.Max(1, (uint)FluidSurface.ActualWidth);
            var height = Math.Max(1, (uint)FluidSurface.ActualHeight);
            var shaderDirectory = System.IO.Path.Combine(AppContext.BaseDirectory, "native", "shaders");
            var hr = NativeInitialize(width, height, shaderDirectory);
            if (hr != 0)
            {
                var message = $"D3D12 initialization failed: 0x{unchecked((uint)hr):X8}";
                StatusText.Text = message;
                Console.WriteLine(message);
                var report = new StringBuilder(4096);
                var reportHr = NativeGetInitializationReport(report, (uint)report.Capacity);
                if (reportHr == 0)
                    Console.WriteLine(report.ToString());
                return;
            }

            hr = NativeGetSwapChain(out var swapChain);
            if (hr != 0)
            {
                var message = $"D3D12 swap-chain export failed: 0x{unchecked((uint)hr):X8}";
                StatusText.Text = message;
                Console.WriteLine(message);
                return;
            }

            _panelNative = FluidSurface.As<ISwapChainPanelNative>();
            hr = _panelNative.SetSwapChain(swapChain);
            if (hr != 0)
            {
                var message = $"D3D12 SwapChainPanel handoff failed: 0x{unchecked((uint)hr):X8}";
                StatusText.Text = message;
                Console.WriteLine(message);
                Marshal.Release(swapChain);
                return;
            }

            _ownedSwapChain = swapChain;
            hr = NativeGetDeviceRemovedReason();
            Console.WriteLine($"D3D12 removed reason after SwapChainPanel handoff: 0x{unchecked((uint)hr):X8}");

            hr = NativeReset();
            if (hr != 0)
            {
                var message = $"D3D12 first render failed: 0x{unchecked((uint)hr):X8}";
                StatusText.Text = message;
                Console.WriteLine(message);
                var report = new StringBuilder(4096);
                if (NativeGetInitializationReport(report, (uint)report.Capacity) == 0)
                    Console.WriteLine(report.ToString());
                return;
            }

            _initialized = true;
            UpdateStatus();
            Console.WriteLine(StatusText.Text);
        }
        catch (Exception ex)
        {
            StatusText.Text = $"D3D12 initialization failed: {ex.Message}";
        }
    }

    private void Timer_Tick(object? sender, object e)
    {
        if (_running)
        {
            var hr = NativeStepBatch(_batchSize);
            if (hr != 0)
            {
                _running = false;
                _timer.Stop();
                StatusText.Text = $"D3D12 step failed: 0x{unchecked((uint)hr):X8}";
                return;
            }
            UpdateStatus();
        }
    }

    private void RunButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized)
            return;
        _running = !_running;
        RunButton.Content = _running ? "Pause" : "Run";
        if (_running)
            _timer.Start();
        else
            _timer.Stop();
    }

    private void StepButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized)
            return;
        var hr = NativeStepBatch(_batchSize);
        if (hr == 0)
            UpdateStatus();
        else
            StatusText.Text = $"D3D12 step failed: 0x{unchecked((uint)hr):X8}";
    }

    private void PourButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized)
            return;
        var number = PourDepthBox.Value;
        var inches = double.IsFinite(number) ? (float)number : 12f;
        var hr = NativePour(inches);
        if (hr == 0)
            UpdateStatus();
        else
            StatusText.Text = $"D3D12 pour failed: 0x{unchecked((uint)hr):X8}";
    }

    private void ResetButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_initialized)
            return;
        _running = false;
        _timer.Stop();
        RunButton.Content = "Run";
        var hr = NativeReset();
        if (hr == 0)
            UpdateStatus();
        else
            StatusText.Text = $"D3D12 reset failed: 0x{unchecked((uint)hr):X8}";
    }

    private void BatchSizeBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_initialized || BatchSizeBox.SelectedItem is not ComboBoxItem item)
            return;
        if (uint.TryParse(item.Tag?.ToString(), out var batchSize))
            _batchSize = batchSize is 1 or 2 or 4 or 8 ? batchSize : 1;
        UpdateStatus();
    }

    private void CpuReferenceToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_initialized)
            return;
        _cpuReference = CpuReferenceToggle.IsOn;
        var hr = NativeSetCpuReference(_cpuReference ? 1 : 0);
        if (hr == 0)
            UpdateStatus();
        else
            StatusText.Text = $"D3D12 CPU reference failed: 0x{unchecked((uint)hr):X8}";
    }

    private void FlowSettings_ValueChanged(NumberBox sender, NumberBoxValueChangedEventArgs args)
    {
        if (!_initialized)
            return;
        var maxFlow = double.IsFinite(MaxFlowBox.Value) ? (float)MaxFlowBox.Value : 2f;
        var settleRate = double.IsFinite(SettleRateBox.Value) ? (float)SettleRateBox.Value : 0.5f;
        var hr = NativeSetSettings(maxFlow, settleRate);
        if (hr != 0)
            StatusText.Text = $"D3D12 settings rejected: 0x{unchecked((uint)hr):X8}";
    }

    private void ViewModeBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        ApplyView();
    }

    private void TerrainToolBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TerrainToolBox.SelectedItem is ComboBoxItem item)
            _terrainTool = item.Tag?.ToString() ?? "raise";
    }

    private void ViewSlider_ValueChanged(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        ApplyView();
    }

    private void ApplyView()
    {
        if (!_initialized)
            return;
        var mode = ViewModeBox.SelectedItem is ComboBoxItem item &&
                   item.Tag?.ToString() == "1" ? 1u : 0u;
        var hr = NativeSetView(
            (float)PanXSlider.Value,
            (float)PanYSlider.Value,
            (float)ZoomSlider.Value,
            mode);
        if (hr == 0)
        {
            hr = NativeSetOrbit(
                (float)YawSlider.Value,
                (float)ElevationSlider.Value);
        }
        if (hr != 0)
            StatusText.Text = $"D3D12 view update failed: 0x{unchecked((uint)hr):X8}";
    }

    private void ResetViewButton_Click(object sender, RoutedEventArgs e)
    {
        _cameraGesture = null;
        YawSlider.Value = 0;
        ElevationSlider.Value = 80;
        PanXSlider.Value = 96;
        PanYSlider.Value = 64;
        ZoomSlider.Value = 1;
        ApplyView();
    }

    private Windows.Foundation.Point NormalizeCapturedPoint(Windows.Foundation.Point position)
    {
        return NormalizeSurfacePoint(position);
    }

    private void FieldCoordinateAtPoint(
        Windows.Foundation.Point position,
        out double fieldX,
        out double fieldY)
    {
        var normalized = NormalizeCapturedPoint(position);
        fieldX = _view_field_x(normalized.X, normalized.Y);
        fieldY = _view_field_y(normalized.X, normalized.Y);
    }

    private double _view_field_x(double normalizedX, double normalizedY)
    {
        var yawRadians = YawSlider.Value * Math.PI / 180.0;
        var elevationRadians = ElevationSlider.Value * Math.PI / 180.0;
        var rightX = Math.Cos(yawRadians);
        var rightY = Math.Sin(yawRadians);
        var forwardX = -rightY;
        var forwardY = rightX;
        var screenX = (normalizedX - 0.5) * 192.0 / Math.Max(1.0, ZoomSlider.Value);
        var screenY = (normalizedY - 0.5) * 128.0 / Math.Max(1.0, ZoomSlider.Value);
        return PanXSlider.Value + rightX * screenX + forwardX * screenY * Math.Sin(elevationRadians);
    }

    private double _view_field_y(double normalizedX, double normalizedY)
    {
        var yawRadians = YawSlider.Value * Math.PI / 180.0;
        var elevationRadians = ElevationSlider.Value * Math.PI / 180.0;
        var rightX = Math.Cos(yawRadians);
        var rightY = Math.Sin(yawRadians);
        var forwardX = -rightY;
        var forwardY = rightX;
        var screenX = (normalizedX - 0.5) * 192.0 / Math.Max(1.0, ZoomSlider.Value);
        var screenY = (normalizedY - 0.5) * 128.0 / Math.Max(1.0, ZoomSlider.Value);
        return PanYSlider.Value + rightY * screenX + forwardY * screenY * Math.Sin(elevationRadians);
    }

    private void ApplyViewKeepingFieldPoint(
        double anchorFieldX,
        double anchorFieldY,
        double normalizedX,
        double normalizedY,
        double yawDegrees,
        double elevationDegrees,
        double zoom)
    {
        var yawRadians = yawDegrees * Math.PI / 180.0;
        var elevationRadians = elevationDegrees * Math.PI / 180.0;
        var rightX = Math.Cos(yawRadians);
        var rightY = Math.Sin(yawRadians);
        var forwardX = -rightY;
        var forwardY = rightX;
        var screenX = (normalizedX - 0.5) * 192.0 / zoom;
        var screenY = (normalizedY - 0.5) * 128.0 / zoom;
        var panX = anchorFieldX
            - rightX * screenX
            - forwardX * screenY * Math.Sin(elevationRadians);
        var panY = anchorFieldY
            - rightY * screenX
            - forwardY * screenY * Math.Sin(elevationRadians);

        PanXSlider.Value = Math.Clamp(panX, PanXSlider.Minimum, PanXSlider.Maximum);
        PanYSlider.Value = Math.Clamp(panY, PanYSlider.Minimum, PanYSlider.Maximum);
    }

    private void FluidSurface_PointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized)
            return;
        var point = e.GetCurrentPoint(FluidSurface);
        var position = point.Position;
        if (point.Properties.IsMiddleButtonPressed)
        {
            _cameraGesture = "orbit";
            _cameraGestureStart = position;
            _cameraGestureStartYaw = YawSlider.Value;
            _cameraGestureStartElevation = ElevationSlider.Value;
            FieldCoordinateAtPoint(position, out _orbitAnchorX, out _orbitAnchorY);
            FluidSurface.CapturePointer(e.Pointer);
            e.Handled = true;
            return;
        }
        if (point.Properties.IsRightButtonPressed)
        {
            _cameraGesture = "pan";
            _cameraGestureStart = position;
            _cameraGestureStartPanX = PanXSlider.Value;
            _cameraGestureStartPanY = PanYSlider.Value;
            _rightDragOccurred = false;
            FluidSurface.CapturePointer(e.Pointer);
            e.Handled = true;
            return;
        }
        var normalized = NormalizeSurfacePoint(position);
        var normalizedX = (float)normalized.X;
        var normalizedY = (float)normalized.Y;
        var hr = NativeSetSelection(normalizedX, normalizedY);
        if (_terrainTool == "raise")
            hr = NativeEditTerrain(normalizedX, normalizedY, 1);
        else if (_terrainTool == "carve")
            hr = NativeEditTerrain(normalizedX, normalizedY, 0);

        if (e.GetCurrentPoint(FluidSurface).Properties.IsLeftButtonPressed &&
            _terrainTool == "select")
        {
            UpdateCellStatus(normalizedX, normalizedY);
        }

        if (hr != 0)
            StatusText.Text = $"D3D12 surface action failed: 0x{unchecked((uint)hr):X8}";
        else
            UpdateStatus();
    }

    private void FluidSurface_PointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized)
            return;
        var point = e.GetCurrentPoint(FluidSurface);
        if (_cameraGesture == "orbit")
        {
            var dx = point.Position.X - _cameraGestureStart.X;
            var dy = point.Position.Y - _cameraGestureStart.Y;
            var yaw = _cameraGestureStartYaw - dx * 0.35;
            yaw = (yaw % 360 + 360) % 360;
            var elevation = Math.Clamp(
                _cameraGestureStartElevation + dy * 0.25,
                ElevationSlider.Minimum,
                ElevationSlider.Maximum);
            var orbitAnchorNormalized = NormalizeSurfacePoint(_cameraGestureStart);

            YawSlider.Value = yaw;
            ElevationSlider.Value = elevation;
            ApplyViewKeepingFieldPoint(
                _orbitAnchorX,
                _orbitAnchorY,
                orbitAnchorNormalized.X,
                orbitAnchorNormalized.Y,
                yaw,
                elevation,
                ZoomSlider.Value);
            e.Handled = true;
            return;
        }
        if (_cameraGesture == "pan")
        {
            var dx = point.Position.X - _cameraGestureStart.X;
            var dy = point.Position.Y - _cameraGestureStart.Y;
            if (Math.Abs(dx) > 3 || Math.Abs(dy) > 3)
                _rightDragOccurred = true;
            var width = Math.Max(1.0, FluidSurface.ActualWidth);
            var height = Math.Max(1.0, FluidSurface.ActualHeight);
            var zoom = Math.Max(1.0, ZoomSlider.Value);
            var yawRadians = YawSlider.Value * Math.PI / 180.0;
            var elevationRadians = ElevationSlider.Value * Math.PI / 180.0;
            var rightX = Math.Cos(yawRadians);
            var rightY = Math.Sin(yawRadians);
            var forwardX = -rightY;
            var forwardY = rightX;
            var screenDeltaX = -dx * 192.0 / width / zoom;
            var screenDeltaY = -dy * 128.0 / height / zoom;
            var panDeltaX = rightX * screenDeltaX
                + forwardX * screenDeltaY * Math.Sin(elevationRadians);
            var panDeltaY = rightY * screenDeltaX
                + forwardY * screenDeltaY * Math.Sin(elevationRadians);
            PanXSlider.Value = Math.Clamp(
                _cameraGestureStartPanX + panDeltaX,
                PanXSlider.Minimum,
                PanXSlider.Maximum);
            PanYSlider.Value = Math.Clamp(
                _cameraGestureStartPanY + panDeltaY,
                PanYSlider.Minimum,
                PanYSlider.Maximum);
            e.Handled = true;
            return;
        }
        if (!point.Properties.IsLeftButtonPressed)
            return;
        var terrainPointNormalized = NormalizeSurfacePoint(point.Position);
        var normalizedX = (float)terrainPointNormalized.X;
        var normalizedY = (float)terrainPointNormalized.Y;
        if (_terrainTool == "raise")
            NativeEditTerrain(normalizedX, normalizedY, 1);
        else if (_terrainTool == "carve")
            NativeEditTerrain(normalizedX, normalizedY, 0);
    }

    private void FluidSurface_PointerReleased(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized)
        {
            return;
        }
        if (_cameraGesture != null)
        {
            FluidSurface.ReleasePointerCapture(e.Pointer);
            _cameraGesture = null;
            e.Handled = true;
            return;
        }
        var normalized = NormalizeSurfacePoint(e.GetCurrentPoint(FluidSurface).Position);
        UpdateCellStatus((float)normalized.X, (float)normalized.Y);
    }

    private void FluidSurface_PointerCanceled(object sender, PointerRoutedEventArgs e)
    {
        CancelCameraGesture(e.Pointer);
    }

    private void FluidSurface_PointerCaptureLost(object sender, PointerRoutedEventArgs e)
    {
        CancelCameraGesture(e.Pointer);
    }

    private void CancelCameraGesture(Microsoft.UI.Xaml.Input.Pointer pointer)
    {
        if (_cameraGesture == null)
            return;
        _cameraGesture = null;
    }

    private void FluidSurface_PointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        if (!_initialized)
            return;
        var delta = e.GetCurrentPoint(FluidSurface).Properties.MouseWheelDelta;
        var normalized = NormalizeSurfacePoint(e.GetCurrentPoint(FluidSurface).Position);
        var zoomBefore = ZoomSlider.Value;
        var anchorFieldX = _view_field_x(normalized.X, normalized.Y);
        var anchorFieldY = _view_field_y(normalized.X, normalized.Y);
        var value = zoomBefore + (delta > 0 ? 0.25 : -0.25);
        var zoom = Math.Clamp(value, ZoomSlider.Minimum, ZoomSlider.Maximum);

        ZoomSlider.Value = zoom;
        ApplyViewKeepingFieldPoint(
            anchorFieldX,
            anchorFieldY,
            normalized.X,
            normalized.Y,
            YawSlider.Value,
            ElevationSlider.Value,
            zoom);
        e.Handled = true;
    }

    private void FluidSurface_RightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        if (!_initialized)
            return;
        if (_rightDragOccurred)
        {
            _rightDragOccurred = false;
            e.Handled = true;
            return;
        }
        var normalized = NormalizeSurfacePoint(e.GetPosition(FluidSurface));
        var hr = NativePourAt((float)normalized.X, (float)normalized.Y, CurrentPourDepth());
        if (hr == 0)
            UpdateStatus();
        else
            StatusText.Text = $"D3D12 pour failed: 0x{unchecked((uint)hr):X8}";
    }

    private Windows.Foundation.Point NormalizeSurfacePoint(Windows.Foundation.Point position)
    {
        var width = Math.Max(1.0, FluidSurface.ActualWidth);
        var height = Math.Max(1.0, FluidSurface.ActualHeight);
        return new Windows.Foundation.Point(
            Math.Clamp(position.X / width, 0.0, 1.0),
            Math.Clamp(position.Y / height, 0.0, 1.0));
    }

    private float CurrentPourDepth()
    {
        var number = PourDepthBox.Value;
        return double.IsFinite(number) ? (float)number : 12f;
    }

    private void UpdateCellStatus(float normalizedX, float normalizedY)
    {
        if (!_initialized)
            return;
        var x = (uint)Math.Clamp((int)Math.Round(normalizedX * 192), 0, 191);
        var y = (uint)Math.Clamp((int)Math.Round(normalizedY * 128), 0, 127);
        var hr = NativeGetCellStatus(x, y, out var cell);
        if (hr == 0)
        {
            var cpuText = _cpuReference
                ? $" · CPU water {cell.CpuWater:F3} in"
                : string.Empty;
            SelectionStatusText.Text =
                $"Cell ({cell.X}, {cell.Y}) · terrain {cell.Terrain} in · water {cell.Water:F3} in{cpuText}";
        }
    }

    private void UpdateStatus()
    {
        var hr = NativeGetStatus(out var status);
        if (hr != 0)
        {
            StatusText.Text = $"D3D12 status failed: 0x{unchecked((uint)hr):X8}";
            return;
        }

        var cpuText = status.CpuReferenceEnabled == 0
            ? "CPU reference off"
            : $"CPU tick {status.CpuTick}\nCPU wet cells {status.CpuWetCells}\nCPU water {status.CpuTotalWater:F1} cell-inches\nCPU max depth {status.CpuMaximumDepth:F2} in\nMismatch cells {status.MismatchCells}\nMax depth mismatch {status.MaximumDepthMismatch:F4} in";

        StatusText.Text =
            $"D3D12 ready\n" +
            $"Tick {status.Tick}\n" +
            $"Batch {status.BatchSize}\n" +
            $"Wet cells {status.WetCells}\n" +
            $"Water {status.TotalWater:F1} cell-inches\n" +
            $"Max depth {status.MaximumDepth:F2} in\n" +
            $"{cpuText}\n" +
            $"Native step {status.NativeStepMsPerTick:F3} ms/tick\n" +
            $"CPU step {status.CpuStepMsPerTick:F3} ms/tick\n" +
            $"Draw {status.DrawMs:F3} ms";
    }
}
