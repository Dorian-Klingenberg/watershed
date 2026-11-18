using System.Runtime.InteropServices;

using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeStatus
    {
        public uint Tick;
        public uint WetCells;
        public float TotalWater;
        public float MaximumDepth;
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
    private static extern int NativeGetStatus(out NativeStatus status);

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeDispose();

    [DllImport("NativeD3D12FluidRenderer.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int NativeGetDeviceRemovedReason();

    public MainWindow()
    {
        InitializeComponent();
        AppWindow.Resize(new(960, 640));

        FluidSurface.Loaded += FluidSurface_Loaded;
        FluidSurface.SizeChanged += FluidSurface_SizeChanged;
        _timer.Tick += Timer_Tick;
        Closed += (_, _) =>
        {
            _timer.Stop();
            if (_initialized)
                NativeDispose();
        };
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

            var panelNative = FluidSurface.As<ISwapChainPanelNative>();
            hr = panelNative.SetSwapChain(swapChain);
            if (hr != 0)
            {
                var message = $"D3D12 SwapChainPanel handoff failed: 0x{unchecked((uint)hr):X8}";
                StatusText.Text = message;
                Console.WriteLine(message);
                return;
            }

            hr = NativeGetDeviceRemovedReason();
            Console.WriteLine($"D3D12 removed reason after SwapChainPanel handoff: 0x{unchecked((uint)hr):X8}");
            Marshal.Release(swapChain);

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
            var hr = NativeStep();
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
        var hr = NativeStep();
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

    private void UpdateStatus()
    {
        var hr = NativeGetStatus(out var status);
        if (hr != 0)
        {
            StatusText.Text = $"D3D12 status failed: 0x{unchecked((uint)hr):X8}";
            return;
        }

        StatusText.Text =
            $"D3D12 ready\nTick {status.Tick}\nWet cells {status.WetCells}\nWater {status.TotalWater:F1} cell-inches\nMax depth {status.MaximumDepth:F2} in";
    }
}
