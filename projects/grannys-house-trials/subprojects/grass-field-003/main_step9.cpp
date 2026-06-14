// grass-field-003 — Step 9: Pluggable Simulator Interface
//
// What changed in Step 9:
//   - Introduced IFieldSim (sim/i_field_sim.h): a pure-virtual interface that
//     decouples Application from any concrete simulator type.
//   - SimpleErosionSim (sim/simple_erosion_sim.h) is the first IFieldSim adapter.
//     It owns a SimpleErosionField internally; Application never touches the concrete type.
//   - reset_field() now calls m_sim->reset(w, d, heights) instead of constructing
//     a SimpleErosionField directly.
//   - All sim call sites (width, depth, height_at, render_ui) go through the interface.
//   - The renderer pipeline (root signature, PSO) is unchanged from Step 8.
//   - No GPU stats panel yet — that comes in Step 10.
//   - To swap simulators in this step: replace the make_unique<> in the constructor
//     and call reset_field(). Nothing else in Application changes.

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
#include <fstream>
#include <memory>   // std::unique_ptr — new in Step 9
#include <stdexcept>
#include <string>
#include <vector>

// DirectXMath provides XMMATRIX, XMMatrixLookAtRH, XMMatrixPerspectiveFovRH,
// XMMatrixInverse, XMMatrixTranspose — used to build the camera matrices
// we upload to the constant buffer each frame.
#include <DirectXMath.h>

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_dx12.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"

#include "grannys_house_trials/sim/grass_field.h"

// Step 9: pluggable simulator interface and first implementation.
// Application depends only on IFieldSim — never on SimpleErosionField or
// SimpleErosionSim. Swapping simulators requires only changing the make_unique<>
// call in the constructor.
#include "sim/i_field_sim.h"
#include "sim/simple_erosion_sim.h"

// Step 8: interactive orbit camera for right-drag / scroll-wheel navigation.
#include "grannys_house_trials/gfx/orbit_camera.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;
namespace sim = grannys_house_trials::sim;
namespace gfx = grannys_house_trials::gfx;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr wchar_t k_window_class_name[] = L"GrassField003Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 003 — Step 9";

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
// Shader binary loader (Step 5)
// ─────────────────────────────────────────────────────────────────────────────
// Reads a compiled shader object (.cso) into a byte vector.
// D3D12 PSO creation takes a {pointer, size} pair — we pass .data() and .size().
static std::vector<uint8_t> load_shader_blob(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader blob: " + path);

    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> blob(size);
    file.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(size));
    return blob;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene constants (Step 5)
// ─────────────────────────────────────────────────────────────────────────────
// This struct is uploaded to the GPU every frame as the cbuffer SceneConstants
// in grass_field_renderer.hlsl. The layout must match the HLSL cbuffer exactly:
// field names don't matter, but types and order do.
//
// D3D12 requires constant buffer views to point at 256-byte-aligned addresses.
// We size the upload buffer to k_cb_aligned_size (rounded up to 256) and only
// use the first sizeof(SceneConstants) bytes of it.
struct SceneConstants
{
    // Inverse of (view * projection). Uploaded transposed because DirectXMath
    // is row-major while HLSL cbuffer packing is column-major. The transpose
    // makes mul(row_vec, matrix) in the shader produce the correct result.
    DirectX::XMFLOAT4X4 inverse_view_projection;   // 64 bytes

    // Camera position in world space. Every pixel's ray originates here.
    DirectX::XMFLOAT4   camera_world_pos;           // 16 bytes

    float    field_origin_x;   // world X of column [0, 0]
    float    field_origin_z;   // world Z of column [0, 0]
    float    voxel_size_feet;  // column width in feet
    float    max_height_feet;  // AABB ceiling in feet

    uint32_t field_width;      // columns in X
    uint32_t field_depth;      // columns in Z
    uint32_t pad0;
    uint32_t pad1;
    // Total: 64 + 16 + 32 = 112 bytes. Upload buffer is rounded to 256.
};

constexpr UINT64 k_cb_aligned_size =
    (sizeof(SceneConstants) + 255u) & ~static_cast<UINT64>(255u);

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
// Window procedure (forward declaration)
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

    // The shared shader-visible CBV/SRV/UAV heap.
    // Slot 0: ImGui font atlas SRV  (set up by ImGui_ImplDX12_Init)
    // Slot 1: field column heights SRV  (set up in initialize_field_buffer)
    // Both the terrain draw and ImGui RenderDrawData bind this same heap,
    // so only one SetDescriptorHeaps call is needed per frame.
    ComPtr<ID3D12DescriptorHeap>      imgui_srv_heap;
    UINT                              rtv_descriptor_size  = 0;
    UINT                              srv_descriptor_size  = 0;  // cached in initialize_imgui

    // ── Step 5: shader pipeline ───────────────────────────────────────────────

    // The root signature declares the parameter layout the shaders expect:
    //   param 0 → inline root CBV at b0  (scene constants, no heap slot needed)
    //   param 1 → descriptor table: 1 SRV at t0  (field heights)
    ComPtr<ID3D12RootSignature>       m_root_signature;

    // Pipeline State Object: bundles the compiled VS + PS blobs, rasterizer
    // state, blend state, render-target format, and root signature into one
    // immutable GPU object. Switching PSOs is cheap — the GPU pre-compiles them.
    ComPtr<ID3D12PipelineState>       m_pso;

    // Upload heap buffer for column heights (one float per column, in feet).
    // Kept persistently mapped so the CPU can memcpy new heights each frame.
    ComPtr<ID3D12Resource>            m_field_buffer;
    float*                            m_field_buffer_mapped = nullptr;

    // Upload heap buffer for the SceneConstants cbuffer.
    // Also persistently mapped — written once per frame in update_scene_constants.
    ComPtr<ID3D12Resource>            m_cb_buffer;
    uint8_t*                          m_cb_buffer_mapped = nullptr;

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

    // ── Step 7: mouse state and picking result ────────────────────────────────

    // Cursor position in client-area pixels, updated by WM_MOUSEMOVE.
    // Used every frame to project a picking ray against the column grid.
    int  mouse_x = 0;
    int  mouse_y = 0;

    // Column index under the cursor this frame (in field grid coordinates).
    // Only valid when m_hovered_column_valid is true.
    int  m_hovered_x = 0;
    int  m_hovered_z = 0;

    // False when the projected cursor ray misses the field (out of bounds,
    // ray parallel to ground, or camera is below the field).
    bool m_hovered_column_valid = false;

    // ── Step 8: orbit camera ──────────────────────────────────────────────────

    // The orbit camera stores the view in spherical coordinates:
    // yaw (horizontal rotation), pitch (vertical tilt), and distance from the
    // focus point. Initialised to give roughly the same overhead angle as the
    // fixed camera we used through Step 7.
    gfx::OrbitCamera m_camera{ -90.f, 45.f, 130.f };

    // Tracks whether a right-mouse drag is in progress.
    bool  m_right_dragging = false;
    POINT m_drag_last{};

    // Cached by update_scene_constants() so that update_mouse_picking() can
    // unproject the cursor ray without recomputing camera matrices.
    DirectX::XMFLOAT4X4  m_inv_vp{};
    DirectX::XMFLOAT3    m_camera_eye{};

    // ── Construction / destruction ───────────────────────────────────────────

    // GrassField provides the initial terrain heights used to seed any simulator.
    // m_sim is the active IFieldSim implementation. All rendering, picking, and
    // GPU upload goes through the interface — Application never touches the
    // concrete type. To swap simulators, replace m_sim and call reset_field().
    sim::GrassField                    m_grass_field{100, 100, 1.0f};
    std::unique_ptr<sim::IFieldSim>    m_sim;

    Application()
    {
        g_app = this;

        // Create the default simulator. reset_field() seeds it from m_grass_field.
        m_sim = std::make_unique<sim::SimpleErosionSim>();

        // Delegate seeding to reset_field() so the Reset button and the
        // constructor share exactly the same initialisation logic.
        reset_field();
    }

    // ── Step 9: field reset via interface ─────────────────────────────────────

    // reset_field() seeds the active simulator from the original GrassField heights,
    // discarding all previous simulation state. Called by both the constructor and
    // the "Reset" ImGui button. Because the seeding logic lives here (not inside
    // the simulator), swapping m_sim to a different IFieldSim type and calling
    // reset_field() is all it takes to start a fresh sim on the same terrain.
    void reset_field()
    {
        const int w = m_grass_field.width();
        const int d = m_grass_field.depth();
        std::vector<int> heights;
        heights.reserve(static_cast<std::size_t>(w * d));
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
                heights.push_back(m_grass_field.coarse_top_height_inches_at(x, z));
        m_sim->reset(w, d, std::move(heights));
    }

    // ── Step 7: mouse picking ─────────────────────────────────────────────────

    // update_mouse_picking() runs a ray-vs-ground-plane test each frame to
    // find which column (if any) the cursor is hovering over.
    //
    // Step 8 change: uses m_inv_vp and m_camera_eye cached by
    // update_scene_constants() instead of recomputing camera matrices here.
    // Step 9 change: bounds check uses m_sim->width() / m_sim->depth() through
    // the interface — the concrete type does not matter.
    void update_mouse_picking()
    {
        using namespace DirectX;

        m_hovered_column_valid = false;

        if (height == 0) return;

        // ── 1. Window pixel → NDC ─────────────────────────────────────────────
        const float ndc_x =  (static_cast<float>(mouse_x) / static_cast<float>(width))  * 2.f - 1.f;
        const float ndc_y = -(static_cast<float>(mouse_y) / static_cast<float>(height)) * 2.f + 1.f;

        // ── 2. Unproject the far-clip point into world space ──────────────────
        XMVECTOR far_clip  = XMVectorSet(ndc_x, ndc_y, 1.f, 1.f);
        XMVECTOR far_world = XMVector4Transform(far_clip, XMLoadFloat4x4(&m_inv_vp));

        const float w_comp = XMVectorGetW(far_world);
        if (std::fabs(w_comp) < 1e-6f) return;
        far_world = XMVectorScale(far_world, 1.f / w_comp);

        // ── 3. Ray direction in world space ───────────────────────────────────
        XMFLOAT3 far_f3;
        XMStoreFloat3(&far_f3, far_world);

        const float dir_x = far_f3.x - m_camera_eye.x;
        const float dir_y = far_f3.y - m_camera_eye.y;
        const float dir_z = far_f3.z - m_camera_eye.z;

        // ── 4. Intersect with Y = 0 (the ground plane) ───────────────────────
        if (std::fabs(dir_y) < 1e-6f) return;
        const float t = -m_camera_eye.y / dir_y;
        if (t < 0.f) return;

        const float hit_x = m_camera_eye.x + t * dir_x;
        const float hit_z = m_camera_eye.z + t * dir_z;

        // ── 5. World-space XZ → column index ─────────────────────────────────
        const float voxel = m_grass_field.voxel_size_feet();
        const int cx = static_cast<int>(hit_x / voxel);
        const int cz = static_cast<int>(hit_z / voxel);

        if (cx < 0 || cx >= m_sim->width())  return;
        if (cz < 0 || cz >= m_sim->depth())  return;

        m_hovered_x             = cx;
        m_hovered_z             = cz;
        m_hovered_column_valid  = true;
    }

    ~Application()
    {
        // Unmap upload buffers before releasing them. The GPU must be idle
        // by the time we call Release() (via ComPtr destructor) on any
        // resource the GPU could still be reading.
        if (m_field_buffer_mapped)
        {
            m_field_buffer->Unmap(0, nullptr);
            m_field_buffer_mapped = nullptr;
        }
        if (m_cb_buffer_mapped)
        {
            m_cb_buffer->Unmap(0, nullptr);
            m_cb_buffer_mapped = nullptr;
        }

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
        throw_if_failed(
            D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
            "D3D12CreateDevice failed. Make sure your GPU supports D3D12.");

        // ── 3. Command Queue ─────────────────────────────────────────────────
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        throw_if_failed(
            device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)),
            "CreateCommandQueue failed.");

        // ── 4. Swap Chain ────────────────────────────────────────────────────
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
                    command_queue.Get(),
                    hwnd,
                    &swap_desc,
                    nullptr,
                    nullptr,
                    &swap_chain1),
                "CreateSwapChainForHwnd failed.");

            throw_if_failed(
                swap_chain1.As(&swap_chain),
                "Failed to query IDXGISwapChain3.");
        }

        frame_index = swap_chain->GetCurrentBackBufferIndex();

        // ── 5. RTV Descriptor Heap ───────────────────────────────────────────
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
            rtv_heap_desc.NumDescriptors = k_frame_count;
            rtv_heap_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            throw_if_failed(
                device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
                "CreateDescriptorHeap (RTV) failed.");

            rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        // ── 6. Render Target Views ───────────────────────────────────────────
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
                rtv_heap->GetCPUDescriptorHandleForHeapStart();

            for (UINT i = 0; i < k_frame_count; ++i)
            {
                throw_if_failed(
                    swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i])),
                    "GetBuffer (render target) failed.");

                device->CreateRenderTargetView(render_targets[i].Get(), nullptr, rtv_handle);
                rtv_handle.ptr += rtv_descriptor_size;
            }
        }

        // ── 7. Command Allocators ────────────────────────────────────────────
        for (UINT i = 0; i < k_frame_count; ++i)
        {
            throw_if_failed(
                device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&frames[i].allocator)),
                "CreateCommandAllocator failed.");
        }

        // ── 8. Command List ──────────────────────────────────────────────────
        throw_if_failed(
            device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                frames[0].allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&command_list)),
            "CreateCommandList failed.");

        throw_if_failed(command_list->Close(), "Initial command list Close() failed.");

        // ── 9. Fence + Event ─────────────────────────────────────────────────
        throw_if_failed(
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "CreateFence failed.");

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
        // Two slots: 0 = ImGui font atlas, 1 = field column heights SRV.
        srv_desc.NumDescriptors = 2;
        srv_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(
            device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&imgui_srv_heap)),
            "CreateDescriptorHeap (SRV heap) failed.");

        srv_descriptor_size = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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

    // initialize_pipeline() builds the root signature and PSO.
    // Unchanged from Step 8 — the raycast renderer still owns the pipeline
    // directly in this step. Step 10 will move this into IFieldRenderer.
    void initialize_pipeline()
    {
        // ── Root signature ────────────────────────────────────────────────────
        D3D12_ROOT_PARAMETER params[2] = {};

        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;   // b0 in HLSL
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 1;
        srv_range.BaseShaderRegister                = 0;   // t0 in HLSL
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
        root_sig_desc.NumParameters = 2;
        root_sig_desc.pParameters   = params;
        root_sig_desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS   |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> sig_blob;
        ComPtr<ID3DBlob> error_blob;
        HRESULT hr = D3D12SerializeRootSignature(
            &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &sig_blob, &error_blob);
        if (FAILED(hr))
        {
            const std::string msg = error_blob
                ? std::string(static_cast<const char*>(error_blob->GetBufferPointer()))
                : "D3D12SerializeRootSignature failed (no error blob).";
            throw std::runtime_error(msg);
        }

        throw_if_failed(
            device->CreateRootSignature(
                0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                IID_PPV_ARGS(&m_root_signature)),
            "CreateRootSignature failed.");

        // ── Shader blobs ──────────────────────────────────────────────────────
        wchar_t exe_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        std::wstring exe_dir = exe_path;
        const auto slash_pos = exe_dir.find_last_of(L"\\/");
        if (slash_pos != std::wstring::npos)
            exe_dir.resize(slash_pos + 1);

        auto to_narrow = [](const std::wstring& ws) {
            std::string s(ws.size() * 3 + 1, '\0');
            const int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                s.data(), static_cast<int>(s.size()), nullptr, nullptr);
            s.resize(n > 0 ? n - 1 : 0);
            return s;
        };

        const auto vs_blob = load_shader_blob(to_narrow(exe_dir + L"shaders/grass_field_vs.cso"));
        const auto ps_blob = load_shader_blob(to_narrow(exe_dir + L"shaders/grass_field_ps.cso"));

        // ── PSO ───────────────────────────────────────────────────────────────
        D3D12_BLEND_DESC blend_desc = {};
        blend_desc.RenderTarget[0].BlendEnable           = FALSE;
        blend_desc.RenderTarget[0].LogicOpEnable         = FALSE;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_RASTERIZER_DESC raster_desc = {};
        raster_desc.FillMode              = D3D12_FILL_MODE_SOLID;
        raster_desc.CullMode              = D3D12_CULL_MODE_NONE;
        raster_desc.FrontCounterClockwise = FALSE;
        raster_desc.DepthClipEnable       = TRUE;
        raster_desc.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC ds_desc = {};
        ds_desc.DepthEnable   = FALSE;
        ds_desc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature        = m_root_signature.Get();
        pso_desc.VS                    = { vs_blob.data(), vs_blob.size() };
        pso_desc.PS                    = { ps_blob.data(), ps_blob.size() };
        pso_desc.BlendState            = blend_desc;
        pso_desc.RasterizerState       = raster_desc;
        pso_desc.DepthStencilState     = ds_desc;
        pso_desc.SampleMask            = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets      = 1;
        pso_desc.RTVFormats[0]         = k_back_buffer_format;
        pso_desc.SampleDesc.Count      = 1;
        pso_desc.SampleDesc.Quality    = 0;

        throw_if_failed(
            device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&m_pso)),
            "CreateGraphicsPipelineState failed.");
    }

    // initialize_field_buffer() creates the GPU buffers for heights and constants.
    // Step 9: uses m_sim->width() / m_sim->depth() through the interface so the
    // buffer size is correct regardless of which concrete simulator is active.
    void initialize_field_buffer()
    {
        const int    cell_count     = m_sim->width() * m_sim->depth();
        const UINT64 field_buf_size = static_cast<UINT64>(cell_count) * sizeof(float);

        D3D12_HEAP_PROPERTIES upload_heap = {};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC buf_desc = {};
        buf_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Height             = 1;
        buf_desc.DepthOrArraySize   = 1;
        buf_desc.MipLevels          = 1;
        buf_desc.Format             = DXGI_FORMAT_UNKNOWN;
        buf_desc.SampleDesc.Count   = 1;
        buf_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buf_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        // ── Field heights buffer ──────────────────────────────────────────────
        buf_desc.Width = field_buf_size;
        throw_if_failed(
            device->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&m_field_buffer)),
            "CreateCommittedResource (field heights) failed.");

        D3D12_RANGE no_read = { 0, 0 };
        throw_if_failed(
            m_field_buffer->Map(0, &no_read,
                reinterpret_cast<void**>(&m_field_buffer_mapped)),
            "Map (field heights) failed.");

        // ── Constant buffer ───────────────────────────────────────────────────
        buf_desc.Width = k_cb_aligned_size;
        throw_if_failed(
            device->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&m_cb_buffer)),
            "CreateCommittedResource (constant buffer) failed.");

        throw_if_failed(
            m_cb_buffer->Map(0, &no_read,
                reinterpret_cast<void**>(&m_cb_buffer_mapped)),
            "Map (constant buffer) failed.");

        // ── SRV at heap slot 1 ────────────────────────────────────────────────
        D3D12_CPU_DESCRIPTOR_HANDLE field_srv_cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        field_srv_cpu.ptr += srv_descriptor_size;  // slot 1

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement        = 0;
        srv_desc.Buffer.NumElements         = static_cast<UINT>(cell_count);
        srv_desc.Buffer.StructureByteStride = sizeof(float);
        srv_desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(m_field_buffer.Get(), &srv_desc, field_srv_cpu);

        update_field_buffer();
    }

    // ── Step 8: per-frame camera input ───────────────────────────────────────

    // handle_camera_input() reads ImGui's mouse state and drives the orbit
    // camera. Must be called AFTER ImGui::NewFrame() so ImGui's IO is populated.
    void handle_camera_input()
    {
        ImGuiIO& io = ImGui::GetIO();

        if (io.WantCaptureMouse)
        {
            m_right_dragging = false;
            return;
        }

        const POINT cur_pos{
            static_cast<LONG>(io.MousePos.x),
            static_cast<LONG>(io.MousePos.y)
        };

        // ── Right-drag: orbit ─────────────────────────────────────────────────
        const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (right_down)
        {
            if (m_right_dragging)
            {
                const int dx = cur_pos.x - m_drag_last.x;
                const int dy = cur_pos.y - m_drag_last.y;
                m_camera.orbit(
                    static_cast<float>(-dx) * 0.35f,
                    static_cast<float>( dy) * 0.25f);
            }
            m_drag_last      = cur_pos;
            m_right_dragging = true;
        }
        else
        {
            m_right_dragging = false;
        }

        // ── Scroll wheel: zoom ────────────────────────────────────────────────
        if (io.MouseWheel != 0.f)
            m_camera.zoom(-io.MouseWheel * 1.2f);
    }

    // ── Step 5: per-frame update methods ─────────────────────────────────────

    // update_scene_constants() computes orbit camera matrices and writes
    // the constant buffer. Also caches m_inv_vp and m_camera_eye for picking.
    void update_scene_constants()
    {
        using namespace DirectX;

        const float aspect = (height > 0)
            ? static_cast<float>(width) / static_cast<float>(height)
            : 1.f;

        const float yaw   = XMConvertToRadians(m_camera.yaw_degrees());
        const float pitch = XMConvertToRadians(m_camera.pitch_degrees());

        const XMFLOAT3 focus_f{ m_camera.focus_x(), m_camera.focus_y(), m_camera.focus_z() };
        const XMFLOAT3 eye_f3{
            focus_f.x + m_camera.distance() * cosf(yaw) * cosf(pitch),
            focus_f.y + m_camera.distance() * sinf(pitch),
            focus_f.z + m_camera.distance() * sinf(yaw) * cosf(pitch)
        };

        const XMVECTOR eye    = XMLoadFloat3(&eye_f3);
        const XMVECTOR target = XMLoadFloat3(&focus_f);
        const XMVECTOR up     = XMVectorSet(0.f, 1.f, 0.f, 0.f);

        const XMMATRIX view   = XMMatrixLookAtRH(eye, target, up);
        const XMMATRIX proj   = XMMatrixPerspectiveFovRH(
            XMConvertToRadians(45.f), aspect, 0.1f, 500.f);

        const XMMATRIX vp     = XMMatrixMultiply(view, proj);
        const XMMATRIX inv_vp = XMMatrixInverse(nullptr, vp);

        // Cache for CPU-side mouse picking.
        XMStoreFloat4x4(&m_inv_vp, inv_vp);
        m_camera_eye = eye_f3;

        SceneConstants cb = {};
        XMStoreFloat4x4(&cb.inverse_view_projection, XMMatrixTranspose(inv_vp));
        XMStoreFloat4(&cb.camera_world_pos, eye);

        cb.field_origin_x  = 0.f;
        cb.field_origin_z  = 0.f;
        cb.voxel_size_feet = m_grass_field.voxel_size_feet();
        cb.max_height_feet = 12.f;
        // Step 9: field dimensions come from the IFieldSim interface.
        cb.field_width     = static_cast<uint32_t>(m_sim->width());
        cb.field_depth     = static_cast<uint32_t>(m_sim->depth());

        memcpy(m_cb_buffer_mapped, &cb, sizeof(cb));
    }

    // update_field_buffer() uploads per-column heights through the IFieldSim interface.
    // Step 9: height_at() is a virtual call, but it runs once per column per frame
    // (10,000 calls for a 100×100 field) and the overhead is negligible.
    void update_field_buffer()
    {
        const int w = m_sim->width();
        const int d = m_sim->depth();

        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
                m_field_buffer_mapped[z * w + x] =
                    m_sim->height_at(x, z) / 12.f;
    }

    // draw_field() records the terrain draw call into the open command list.
    // Unchanged from Step 8 — raycast renderer, full-screen triangle.
    void draw_field()
    {
        command_list->SetGraphicsRootSignature(m_root_signature.Get());
        command_list->SetDescriptorHeaps(1, imgui_srv_heap.GetAddressOf());
        command_list->SetGraphicsRootConstantBufferView(
            0, m_cb_buffer->GetGPUVirtualAddress());

        D3D12_GPU_DESCRIPTOR_HANDLE field_srv_gpu =
            imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();
        field_srv_gpu.ptr += srv_descriptor_size;
        command_list->SetGraphicsRootDescriptorTable(1, field_srv_gpu);

        command_list->SetPipelineState(m_pso.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VIEWPORT vp = {
            0.f, 0.f,
            static_cast<float>(width), static_cast<float>(height),
            0.f, 1.f
        };
        D3D12_RECT scissor = { 0, 0,
            static_cast<LONG>(width), static_cast<LONG>(height) };
        command_list->RSSetViewports(1, &vp);
        command_list->RSSetScissorRects(1, &scissor);

        command_list->DrawInstanced(3, 1, 0, 0);
    }

    // ── GPU synchronisation ───────────────────────────────────────────────────
    void wait_for_gpu()
    {
        throw_if_failed(
            command_queue->Signal(fence.Get(), fence_value),
            "Signal (wait_for_gpu) failed.");

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

    void begin_frame()
    {
        FrameContext& frame = frames[frame_index];

        if (frame.fence_value != 0 && fence->GetCompletedValue() < frame.fence_value)
        {
            throw_if_failed(
                fence->SetEventOnCompletion(frame.fence_value, fence_event),
                "SetEventOnCompletion (begin_frame) failed.");
            WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        }

        throw_if_failed(frame.allocator->Reset(), "Allocator Reset failed.");
        throw_if_failed(
            command_list->Reset(frame.allocator.Get(), nullptr),
            "CommandList Reset failed.");

        // Resource barrier: PRESENT → RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = render_targets[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;

        command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    }

    void end_frame()
    {
        // Resource barrier: RENDER_TARGET → PRESENT
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = render_targets[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);

        throw_if_failed(command_list->Close(), "CommandList Close failed.");

        ID3D12CommandList* lists[] = { command_list.Get() };
        command_queue->ExecuteCommandLists(1, lists);

        throw_if_failed(swap_chain->Present(1, 0), "Present failed.");

        frames[frame_index].fence_value = fence_value;
        throw_if_failed(
            command_queue->Signal(fence.Get(), fence_value),
            "Signal (end_frame) failed.");
        ++fence_value;

        frame_index = swap_chain->GetCurrentBackBufferIndex();
    }

    void render_imgui()
    {
        // handle_camera_input() must run after ImGui::NewFrame() so ImGui's IO is ready.
        handle_camera_input();

        // ── Renderer info panel ───────────────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(60, 240), ImGuiCond_FirstUseEver);
        ImGui::Begin("Renderer");
        ImGui::Text("Frame index: %u", frame_index);
        ImGui::Text("Back buffer: %u x %u", width, height);
        ImGui::End();

        // ── Simulation panel ──────────────────────────────────────────────────
        //
        // The panel has two parts:
        //   1. A header showing grid dimensions and the Reset button — these
        //      are Application-level concerns that don't vary by simulator type.
        //   2. m_sim->render_ui() — everything that IS specific to the active
        //      simulator (cycle count, step buttons, tuning sliders, etc.).
        //
        // Nothing in Application knows which concrete simulator is running.
        ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
        ImGui::Begin("Simulation");

        ImGui::Text("Field:  %d x %d columns",
            m_sim->width(), m_sim->depth());
        ImGui::Text("Simulator: %s", m_sim->name());

        // Reset re-seeds the active simulator from the original GrassField heights,
        // discarding all accumulated simulation state.
        if (ImGui::Button("Reset"))
            reset_field();

        ImGui::Separator();

        // Delegate all simulator-specific UI to the simulator itself.
        // To add a new simulator type: implement IFieldSim, swap m_sim here,
        // call reset_field(). No other changes needed.
        m_sim->render_ui();

        ImGui::End();

        // ── Camera panel ──────────────────────────────────────────────────────
        ImGui::Begin("Camera");
        ImGui::Text("Yaw:      %.1f deg", m_camera.yaw_degrees());
        ImGui::Text("Pitch:    %.1f deg", m_camera.pitch_degrees());
        ImGui::Text("Distance: %.1f ft",  m_camera.distance());
        ImGui::Separator();
        ImGui::TextDisabled("Right-drag to orbit");
        ImGui::TextDisabled("Scroll to zoom");
        if (ImGui::Button("Reset Camera"))
            m_camera = gfx::OrbitCamera{ -90.f, 45.f, 130.f };
        ImGui::End();

        // ── Selected column panel ─────────────────────────────────────────────
        ImGui::Begin("Selected Column");
        if (m_hovered_column_valid)
        {
            // Step 9: height_at() goes through IFieldSim — no concrete type needed.
            const int h_inches = m_sim->height_at(m_hovered_x, m_hovered_z);
            ImGui::Text("Column [%d, %d]", m_hovered_x, m_hovered_z);
            ImGui::Text("Height: %d inches", h_inches);
            ImGui::Text("        %.2f feet",  static_cast<float>(h_inches) / 12.f);
        }
        else
        {
            ImGui::TextDisabled("Cursor outside field");
        }
        ImGui::End();

        ImGui::Render();
        command_list->SetDescriptorHeaps(1, imgui_srv_heap.GetAddressOf());
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    void run()
    {
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

            begin_frame();

            {
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
                    rtv_heap->GetCPUDescriptorHandleForHeapStart();
                rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;
                command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
            }

            // update_scene_constants must run before update_mouse_picking so
            // m_inv_vp and m_camera_eye are fresh for the picking ray.
            update_scene_constants();
            update_field_buffer();
            update_mouse_picking();

            draw_field();

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            render_imgui();

            end_frame();
        }

        wait_for_gpu();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
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
        if (g_app && wparam != SIZE_MINIMIZED)
        {
            g_app->width  = static_cast<UINT>(LOWORD(lparam));
            g_app->height = static_cast<UINT>(HIWORD(lparam));
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_app)
        {
            g_app->mouse_x = GET_X_LPARAM(lparam);
            g_app->mouse_y = GET_Y_LPARAM(lparam);
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
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
        app.initialize_pipeline();
        app.initialize_field_buffer();
        app.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Grass Field 003 — Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
