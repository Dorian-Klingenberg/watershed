// grass-field-003 — Step 4: Simulation Data (SimpleErosionField)

// ─────────────────────────────────────────────────────────────────────────────
// Preprocessor guards (unchanged from Step 1)
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <windowsx.h>

#include <CommCtrl.h>

// ─────────────────────────────────────────────────────────────────────────────
// D3D12 and DXGI headers  (new in Step 2)
// ─────────────────────────────────────────────────────────────────────────────

// Core D3D12 types: ID3D12Device, ID3D12CommandQueue, ID3D12CommandAllocator,
// ID3D12GraphicsCommandList, ID3D12DescriptorHeap, ID3D12Fence, ...
#include <d3d12.h>

// DXGI presentation layer: IDXGIFactory, IDXGISwapChain3, DXGI_SWAP_EFFECT_*
// The "1_6" header is cumulative; IDXGISwapChain3 lives in dxgi1_4.h but
// including dxgi1_6.h gives you everything up to that version.
#include <dxgi1_6.h>

// ComPtr<T>: a lightweight RAII wrapper for COM interface pointers.
// On destruction it calls Release() automatically. This is the right tool
// for every D3D12 / DXGI interface in this file.
#include <wrl/client.h>

#include <array>
#include <stdexcept>

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx12.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include "grannys_house_trials/sim/grass_field.h"
#include "sim/simple_erosion_field.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;
namespace sim = grannys_house_trials::sim;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr wchar_t k_window_class_name[] = L"GrassField003Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 003 — Step 4";

// Double-buffering: two back buffers. While the GPU renders into frame N,
// the CPU can record commands for frame N+1. Using more buffers reduces
// stalls but increases memory; 2 is the standard starting point.
constexpr UINT k_frame_count = 2;

// The back-buffer pixel format. R8G8B8A8_UNORM is the most portable choice;
// every GPU and monitor chain supports it.
constexpr DXGI_FORMAT k_back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: throw on HRESULT failure
// ─────────────────────────────────────────────────────────────────────────────
// Nearly every D3D12 call returns HRESULT. Wrapping the check in a helper
// keeps call sites readable and gives a meaningful error string in the
// MessageBox when something goes wrong.
static void throw_if_failed(HRESULT hr, const char* message)
{
    if (FAILED(hr))
        throw std::runtime_error(message);
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame resources
// ─────────────────────────────────────────────────────────────────────────────
// D3D12 requires one command allocator per frame in flight. An allocator owns
// the memory backing its command list's recorded commands. You must NOT reset
// an allocator while the GPU is still executing commands from it — hence the
// fence_value tracking below.
struct FrameContext
{
    // The allocator for this frame's command list recording.
    ComPtr<ID3D12CommandAllocator> allocator;

    // The fence value the GPU must have passed before this allocator can be
    // reused. Set to 0 initially (no work submitted yet).
    UINT64 fence_value = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Forward declaration for the application state
// ─────────────────────────────────────────────────────────────────────────────
struct Application;
Application* g_app = nullptr;  // Window proc needs access before D3D12 is ready.

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure (expanded from Step 1)
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────
struct Application
{
    // ── Win32 state ──────────────────────────────────────────────────────────
    HWND hwnd   = nullptr;
    UINT width  = 1280;
    UINT height = 720;

    // ── D3D12 core objects ───────────────────────────────────────────────────
    //
    // Ownership order (also the shutdown order, reversed):
    //   factory → device → command_queue → swap_chain
    //                    → rtv_heap
    //                    → frames[i].allocator
    //                    → command_list
    //                    → fence / fence_event
    //
    // All are ComPtr<> so they Release() automatically when Application
    // is destroyed — BUT you must drain the GPU first (see ~Application).

    ComPtr<IDXGIFactory4>             factory;
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        command_queue;
    ComPtr<IDXGISwapChain3>           swap_chain;

    // The RTV heap holds k_frame_count render-target-view descriptors —
    // one per back buffer. A descriptor is a small opaque GPU-side token
    // that tells the pipeline "bind this resource as a colour render target".
    ComPtr<ID3D12DescriptorHeap>      rtv_heap;
    ComPtr<ID3D12DescriptorHeap>      imgui_srv_heap;
    UINT                              rtv_descriptor_size = 0;

    // One render target resource (back buffer) per frame.
    std::array<ComPtr<ID3D12Resource>, k_frame_count> render_targets;

    // Per-frame allocator + fence tracking.
    std::array<FrameContext, k_frame_count> frames;

    // A single command list is reused each frame: Reset() it with the
    // current frame's allocator, record commands, Close(), Execute.
    ComPtr<ID3D12GraphicsCommandList> command_list;

    // The fence is a GPU/CPU synchronisation primitive.
    // command_queue->Signal(fence, value) writes `value` to the fence
    // when all prior GPU work is done. fence->GetCompletedValue() lets
    // the CPU poll it without blocking.
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fence_event  = nullptr;
    UINT64                            fence_value  = 1;

    // Current back buffer index (0 or 1 with k_frame_count = 2).
    // Updated after every Present() via GetCurrentBackBufferIndex().
    UINT frame_index = 0;

    // ── Construction / destruction ───────────────────────────────────────────

    // GrassField provides the initial terrain heights; SimpleErosionField
    // is our own implementation of the gravity-settling algorithm.
    // GrassField is declared first — we read from it during seeding.
    sim::GrassField          m_grass_field{100, 100, 1.0f};
    sim::SimpleErosionField  m_erosion_field{};   // seeded in Application()

    Application()
    {
        g_app = this;

        // Seed the erosion field from the GrassField's coarse column heights.
        // We iterate in the same (z, x) row-major order that SimpleErosionField
        // expects so the flat index_of() mapping stays consistent.
        const int w = m_grass_field.width();
        const int d = m_grass_field.depth();
        std::vector<int> heights;
        heights.reserve(static_cast<std::size_t>(w * d));
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
                heights.push_back(m_grass_field.coarse_top_height_inches_at(x, z));

        m_erosion_field = sim::SimpleErosionField(w, d, std::move(heights));
    }

    ~Application()
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // CRITICAL: wait for the GPU to finish ALL in-flight work before
        // any ComPtr<> destructor fires. If you destroy a resource that
        // the GPU is still reading from, you get a device-removed error
        // or a crash in the driver. This is the D3D12 "shutdown fence".
        if (fence && fence_event)
            wait_for_gpu();

        if (fence_event)
        {
            CloseHandle(fence_event);
            fence_event = nullptr;
        }

        g_app = nullptr;
    }

    // ── Win32 window creation (same as Step 1) ───────────────────────────────

    void create_window()
    {
        WNDCLASSEXW wnd_class  = {};
        wnd_class.cbSize        = sizeof(WNDCLASSEXW);
        wnd_class.style         = CS_HREDRAW | CS_VREDRAW;
        wnd_class.lpfnWndProc   = window_proc;
        wnd_class.hInstance     = GetModuleHandleW(nullptr);
        wnd_class.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wnd_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wnd_class.lpszClassName = k_window_class_name;

        if (!RegisterClassExW(&wnd_class))
            throw std::runtime_error("RegisterClassExW failed.");

        hwnd = CreateWindowExW(
            0, k_window_class_name, k_window_title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            static_cast<int>(width), static_cast<int>(height),
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (!hwnd)
            throw std::runtime_error("CreateWindowExW failed.");

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    // ── D3D12 initialisation (new in Step 2) ─────────────────────────────────

    void initialize_d3d12()
    {
        // ── 1. DXGI Factory ─────────────────────────────────────────────────
        //
        // IDXGIFactory is the entry point for the DXGI layer. It enumerates
        // adapters (GPUs) and creates swap chains. We ask for IDXGIFactory4
        // because that is the minimum version that supports DXGI_SWAP_EFFECT_FLIP_DISCARD
        // (the recommended swap effect since Windows 10).
        //
        // The debug flag enables the DXGI validation layer — it catches
        // incorrect usage (e.g. wrong formats, wrong resource states). Only
        // set it in debug builds; it adds CPU overhead.
        UINT factory_flags = 0;
#if defined(_DEBUG)
        // Enable the D3D12 debug layer first (must happen before device creation).
        {
            ComPtr<ID3D12Debug> debug_controller;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
            {
                debug_controller->EnableDebugLayer();
                factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif
        throw_if_failed(
            CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)),
            "CreateDXGIFactory2 failed.");

        // ── 2. D3D12 Device ─────────────────────────────────────────────────
        //
        // D3D12CreateDevice creates the logical device. Passing nullptr as the
        // adapter selects the default hardware adapter (the primary GPU).
        // D3D_FEATURE_LEVEL_11_0 is the minimum required for D3D12; virtually
        // every DirectX-capable GPU since ~2012 supports it.
        //
        // The device is the factory for almost everything else: heaps,
        // resources, command allocators, fences, descriptor heaps, PSOs.
        // QUESTION: What does upgrading the feature level get us?
        throw_if_failed(
            D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
            "D3D12CreateDevice failed. Make sure your GPU supports D3D12.");

        // ── 3. Command Queue ─────────────────────────────────────────────────
        //
        // The command queue is the channel between the CPU and the GPU.
        // You submit command lists to it; the GPU executes them in order.
        //
        // D3D12_COMMAND_LIST_TYPE_DIRECT: can record all command types
        // (draw, copy, compute). There are also COPY and COMPUTE queues for
        // async transfer/compute; we will not use those yet.
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        throw_if_failed(
            device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)),
            "CreateCommandQueue failed.");

        // ── 4. Swap Chain ────────────────────────────────────────────────────
        //
        // The swap chain owns the back buffers that are displayed on screen.
        // Key fields:
        //
        //   BufferCount = k_frame_count (2): double-buffering.
        //   Format = DXGI_FORMAT_R8G8B8A8_UNORM: standard 32-bit RGBA.
        //   SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD:
        //     The modern "flip model". Each Present() hands the back buffer to
        //     the OS compositor; the previous back buffer is discarded (you must
        //     re-render it next frame). Flip model avoids a blit copy and is
        //     mandatory for variable refresh rate (VRR / G-Sync / FreeSync).
        //
        // We create IDXGISwapChain1 first (the lowest common denominator),
        // then .As() to IDXGISwapChain3 so we can call GetCurrentBackBufferIndex().
        {
            DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
            swap_desc.BufferCount  = k_frame_count;
            swap_desc.Width        = width;
            swap_desc.Height       = height;
            swap_desc.Format       = k_back_buffer_format;
            swap_desc.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_desc.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swap_desc.SampleDesc.Count   = 1;
            swap_desc.SampleDesc.Quality = 0;

            ComPtr<IDXGISwapChain1> swap_chain1;
            throw_if_failed(
                factory->CreateSwapChainForHwnd(
                    command_queue.Get(),   // The queue used to present frames.
                    hwnd,
                    &swap_desc,
                    nullptr,               // No full-screen desc (start windowed).
                    nullptr,               // No output restriction.
                    &swap_chain1),
                "CreateSwapChainForHwnd failed.");

            throw_if_failed(
                swap_chain1.As(&swap_chain),
                "Failed to query IDXGISwapChain3.");
        }

        frame_index = swap_chain->GetCurrentBackBufferIndex();

        // ── 5. RTV Descriptor Heap ───────────────────────────────────────────
        //
        // A descriptor heap is a GPU-visible array of descriptors (metadata
        // structures that tell the GPU how to interpret a resource).
        //
        // We need one RTV (render target view) descriptor per back buffer
        // so the GPU knows the format and dimensions of each render target.
        //
        // D3D12_DESCRIPTOR_HEAP_FLAG_NONE: RTV descriptors are CPU-only;
        // they do not need to be shader-visible (unlike CBV/SRV/UAV heaps
        // that shaders sample from).
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
            rtv_heap_desc.NumDescriptors = k_frame_count;
            rtv_heap_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            throw_if_failed(
                device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
                "CreateDescriptorHeap (RTV) failed.");

            // Descriptor size varies per GPU. Cache it once.
            rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        // ── 6. Render Target Views ───────────────────────────────────────────
        //
        // For each back buffer:
        //   a) Get the ID3D12Resource from the swap chain.
        //   b) Create an RTV descriptor pointing to it in the heap.
        //
        // The CPU descriptor handle is a pointer-like offset into the heap.
        // We stride it by rtv_descriptor_size for each subsequent entry.
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
                rtv_heap->GetCPUDescriptorHandleForHeapStart();

            for (UINT i = 0; i < k_frame_count; ++i)
            {
                throw_if_failed(
                    swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i])),
                    "GetBuffer (render target) failed.");

                // nullptr = use the resource's own format (inherited from swap chain).
                device->CreateRenderTargetView(render_targets[i].Get(), nullptr, rtv_handle);
                rtv_handle.ptr += rtv_descriptor_size;
            }
        }

        // ── 7. Command Allocators ────────────────────────────────────────────
        //
        // Each allocator owns the raw memory for recorded GPU commands.
        // You need one per frame-in-flight so that while the GPU executes
        // frame N's commands, the CPU can safely record frame N+1's commands
        // into a different allocator.
        //
        // RULE: never Reset() an allocator until the GPU has finished all
        // commands recorded into it. Fence tracking (below) enforces this.
        for (UINT i = 0; i < k_frame_count; ++i)
        {
            throw_if_failed(
                device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&frames[i].allocator)),
                "CreateCommandAllocator failed.");
        }

        // ── 8. Command List ──────────────────────────────────────────────────
        //
        // A command list records GPU work: barriers, clears, draws, copies, etc.
        // We create one list and reuse it every frame by Reset()-ing it with
        // the current frame's allocator.
        //
        // CreateCommandList opens the list in a recording state automatically,
        // so we Close() it immediately. begin_frame() will Reset() it before
        // each recording session.
        throw_if_failed(
            device->CreateCommandList(
                0,                              // Node mask (single-GPU: 0).
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                frames[0].allocator.Get(),      // Initial allocator (will be Reset()).
                nullptr,                        // Initial PSO (none yet — no shaders).
                IID_PPV_ARGS(&command_list)),
            "CreateCommandList failed.");

        throw_if_failed(command_list->Close(), "Initial command list Close() failed.");

        // ── 9. Fence + Event ─────────────────────────────────────────────────
        //
        // The fence is the CPU/GPU synchronisation mechanism.
        //
        // Pattern:
        //   CPU records frame N → ExecuteCommandLists → Signal(fence, N)
        //   CPU wants to reuse frame N's allocator for frame N+2:
        //     → if fence.GetCompletedValue() < N, wait on fence_event
        //
        // fence_value starts at 1 so that the initial value of 0 in
        // FrameContext::fence_value means "never submitted" (no wait needed).
        throw_if_failed(
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "CreateFence failed.");

        // CreateEventW creates a Win32 auto-reset event. The fence signals it
        // when GetCompletedValue() reaches the requested value.
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event)
            throw std::runtime_error("CreateEventW (fence event) failed.");
    }

    void initialize_imgui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        if (!ImGui_ImplWin32_Init(hwnd))
            throw std::runtime_error("ImGui_ImplWin32_Init failed.");

        D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
        srv_desc.NumDescriptors = 1;
        srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(
            device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&imgui_srv_heap)),
            "CreateDescriptorHeap (ImGui SRV) failed.");

        if (!ImGui_ImplDX12_Init(
                device.Get(),
                k_frame_count,
                k_back_buffer_format,
                imgui_srv_heap.Get(),
                imgui_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                imgui_srv_heap->GetGPUDescriptorHandleForHeapStart()))
        {
            throw std::runtime_error("ImGui_ImplDX12_Init failed.");
        }
    }

    // ── GPU synchronisation ───────────────────────────────────────────────────
    //
    // Signals the fence with the next value and blocks the CPU until the GPU
    // reaches that value. Used at shutdown and (for simplicity now) after every
    // Present(). In a more optimised renderer you only wait when you need to
    // reuse a specific frame's allocator, not every frame.
    void wait_for_gpu()
    {
        // Signal: asks the GPU to write fence_value into the fence object
        // once it finishes all previously submitted work.
        throw_if_failed(
            command_queue->Signal(fence.Get(), fence_value),
            "Signal (wait_for_gpu) failed.");

        // If the GPU hasn't reached fence_value yet, block the CPU thread.
        if (fence->GetCompletedValue() < fence_value)
        {
            throw_if_failed(
                fence->SetEventOnCompletion(fence_value, fence_event),
                "SetEventOnCompletion failed.");
            WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        }

        ++fence_value;
    }

    // ── Per-frame recording helpers ───────────────────────────────────────────

    // begin_frame(): prepare the command list for a new frame.
    //
    // Steps:
    //   1. Ensure the GPU has finished with this frame's allocator.
    //   2. Reset the allocator (frees its memory for reuse).
    //   3. Reset the command list with the fresh allocator.
    //   4. Transition the back buffer from PRESENT to RENDER_TARGET.
    //   5. Bind the RTV so subsequent clears and draws go to it.
    void begin_frame()
    {
        FrameContext& frame = frames[frame_index];

        // Wait if the GPU is still using this frame's allocator from two
        // frames ago. With k_frame_count=2, this usually does not block.
        if (frame.fence_value != 0 && fence->GetCompletedValue() < frame.fence_value)
        {
            throw_if_failed(
                fence->SetEventOnCompletion(frame.fence_value, fence_event),
                "SetEventOnCompletion (begin_frame) failed.");
            WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        }

        // Reset the allocator now that the GPU is done with its commands.
        throw_if_failed(frame.allocator->Reset(), "Allocator Reset failed.");

        // Reset the command list, associating it with the current allocator.
        // nullptr = no initial PSO (we have no shaders yet).
        throw_if_failed(
            command_list->Reset(frame.allocator.Get(), nullptr),
            "CommandList Reset failed.");

        // ── Resource barrier: PRESENT → RENDER_TARGET ────────────────────────
        //
        // D3D12 requires explicit resource state transitions. When the swap
        // chain owns the back buffer for display it is in the PRESENT state.
        // Before we can write (clear) it we must transition it to RENDER_TARGET.
        //
        // D3D12_RESOURCE_BARRIER_TYPE_TRANSITION means "move this resource
        // from state A to state B". The GPU uses this to know when it is safe
        // to flush caches and change how it accesses the resource.
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = render_targets[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);

        // Compute the CPU handle for this frame's RTV in the heap.
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;

        // Bind the render target. nullptr = no depth-stencil target (yet).
        command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    }

    // end_frame(): finish recording, submit to GPU, present, advance.
    //
    // Steps:
    //   1. Transition back buffer from RENDER_TARGET back to PRESENT.
    //   2. Close the command list.
    //   3. Submit it to the command queue.
    //   4. Present the swap chain.
    //   5. Signal the fence so we can track when this frame completes.
    //   6. Advance frame_index to the next back buffer.
    void end_frame()
    {
        // ── Resource barrier: RENDER_TARGET → PRESENT ────────────────────────
        //
        // Before Present() we must transition the back buffer back to
        // D3D12_RESOURCE_STATE_PRESENT. Missing this transition causes a
        // D3D12 validation error and undefined display behaviour.
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = render_targets[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);

        // Close: seals the command list. A closed list can be submitted but
        // not recorded into. Must be closed before ExecuteCommandLists.
        throw_if_failed(command_list->Close(), "CommandList Close failed.");

        // Submit. ExecuteCommandLists takes an array; we have one list.
        ID3D12CommandList* lists[] = { command_list.Get() };
        command_queue->ExecuteCommandLists(1, lists);

        // Present. sync_interval=1 means vsync (wait for the next vblank).
        // Use 0 for uncapped / tear-allowed (useful for frame-timing tests).
        throw_if_failed(swap_chain->Present(1, 0), "Present failed.");

        // Record which fence value this frame was submitted at so begin_frame()
        // can wait before reusing this allocator.
        frames[frame_index].fence_value = fence_value;

        // Signal: ask the GPU to write fence_value when it finishes this frame.
        throw_if_failed(
            command_queue->Signal(fence.Get(), fence_value),
            "Signal (end_frame) failed.");
        ++fence_value;

        // Advance to the next back buffer.
        frame_index = swap_chain->GetCurrentBackBufferIndex();
    }

    void render_imgui()
    {
        // ── Renderer info panel ───────────────────────────────────────────────
        ImGui::Begin("Renderer");
        ImGui::Text("Frame index: %u", frame_index);
        ImGui::Text("Back buffer: %u x %u", width, height);
        ImGui::End();

        // ── Simulation panel ──────────────────────────────────────────────────
        ImGui::Begin("Simulation");

        ImGui::Text("Erosion cycles: %d", m_erosion_field.cycle_count());
        ImGui::Text("Field: %d x %d columns", m_erosion_field.width(), m_erosion_field.depth());

        if (ImGui::Button("Step Erosion (x1)"))
            m_erosion_field.step_cycle();

        ImGui::SameLine();

        if (ImGui::Button("Step Erosion (x100)"))
            for (int i = 0; i < 100; ++i)
                m_erosion_field.step_cycle();

        ImGui::Separator();
        ImGui::Text("Sample heights (inches) — top-left 3x3:");
        for (int z = 0; z < 3; ++z)
        {
            for (int x = 0; x < 3; ++x)
            {
                ImGui::Text("  [%d,%d] %d\"", x, z, m_erosion_field.height_at(x, z));
                if (x < 2) ImGui::SameLine();
            }
        }

        ImGui::End();

        ImGui::Render();
        command_list->SetDescriptorHeaps(1, imgui_srv_heap.GetAddressOf());
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    void run()
    {
        // Sky-blue clear color. RGBA in [0,1].
        constexpr float clear_color[4] = { 0.53f, 0.81f, 0.98f, 1.0f };

        MSG  msg     = {};
        bool running = true;

        while (running)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running) break;

            // ── Render tick ───────────────────────────────────────────────────
            begin_frame();

            // ClearRenderTargetView fills the bound render target with the
            // given colour. This is all the "rendering" we do in Step 2.
            // The RTV handle must match the one we bound in begin_frame().
            {
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
                    rtv_heap->GetCPUDescriptorHandleForHeapStart();
                rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;
                command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
            }

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            render_imgui();

            end_frame();
            // Step 5: PSO + DrawInstanced goes here.
        }

        // Drain the GPU before the destructor releases COM objects.
        wait_for_gpu();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure (expanded from Step 1)
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
        return 1;

    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        // Record the new dimensions. We do not resize the swap chain here yet
        // (that requires releasing all back-buffer references, calling
        // ResizeBuffers, then recreating the RTVs). That will be added when
        // this project needs it. For now just tracking width/height is enough.
        if (g_app && wparam != SIZE_MINIMIZED)
        {
            g_app->width  = static_cast<UINT>(LOWORD(lparam));
            g_app->height = static_cast<UINT>(HIWORD(lparam));
            // Step 2 TODO: call swap chain ResizeBuffers here.
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point (unchanged from Step 1)
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                    PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
    try
    {
        Application app;
        app.create_window();
        app.initialize_d3d12();
        app.initialize_imgui();
        app.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Grass Field 003 — Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
