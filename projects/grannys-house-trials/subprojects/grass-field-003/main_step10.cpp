// grass-field-003 — Step 10: Pluggable Renderer + GPU Panel

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
#include "sim/i_field_sim.h"
#include "sim/simple_erosion_sim.h"
#include "grannys_house_trials/gfx/orbit_camera.h"

// ── Pluggable renderer (Step 9) ───────────────────────────────────────────────
#include "gfx/i_field_renderer.h"
#include "gfx/raycast_renderer.h"
#include "gfx/wireframe_renderer.h"

#include <memory>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;
namespace sim = grannys_house_trials::sim;
namespace gfx = grannys_house_trials::gfx;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr wchar_t k_window_class_name[] = L"GrassField003Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 003 — Step 10";

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

    // Forward VP matrix added for mesh-based renderers (wireframe, future passes).
    // The raycast PS uses inverse_view_projection; the wireframe VS uses this.
    // Declared at the end so the original 112-byte layout is unchanged for the
    // raycast shader which only reads the first 112 bytes from b0.
    DirectX::XMFLOAT4X4 view_projection;   // 64 bytes — total struct = 176 bytes
    // Upload buffer (k_cb_aligned_size) rounds to 256 bytes regardless.
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

    // The shared shader-visible CBV/SRV/UAV heap.
    // Slot 0: ImGui font atlas SRV  (set up by ImGui_ImplDX12_Init)
    // Slot 1: field column heights SRV  (set up in initialize_field_buffer)
    // Both the terrain draw and ImGui RenderDrawData bind this same heap,
    // so only one SetDescriptorHeaps call is needed per frame.
    ComPtr<ID3D12DescriptorHeap>      imgui_srv_heap;
    UINT                              rtv_descriptor_size  = 0;
    UINT                              srv_descriptor_size  = 0;  // cached in initialize_imgui

    // ── Step 9: pluggable renderers ───────────────────────────────────────────

    // All available renderers, initialized at startup. Switching between them is
    // instant because every renderer is already fully built (no GPU stall needed).
    // To add a new renderer: implement IFieldRenderer, push_back a new instance
    // in initialize_renderers(). Nothing else in Application changes.
    std::vector<std::unique_ptr<gfx::IFieldRenderer>> m_renderers;

    // Index into m_renderers for the currently active renderer.
    // Changed by the ImGui combo in the Renderer panel.
    int m_active_renderer = 0;

    // ── GPU info (queried once at device creation) ────────────────────────────
    // Human-readable adapter description, e.g. "NVIDIA GeForce RTX 4080".
    // Stored as a narrow (UTF-8) string for ImGui::Text.
    std::string m_adapter_name;

    // Dedicated video memory in bytes — reported by DXGI_ADAPTER_DESC1.
    SIZE_T m_adapter_vram_bytes = 0;

    // ── Frame timing ─────────────────────────────────────────────────────────
    // We sample QPC (QueryPerformanceCounter) twice per frame to compute
    // frame time and derive FPS. QPC is the highest-resolution timer on Windows;
    // it does NOT drift even across CPU cores.
    LARGE_INTEGER m_qpc_frequency    = {};   // ticks per second — queried once
    LARGE_INTEGER m_qpc_frame_start  = {};   // QPC value at the start of the last frame

    // Smoothed frame time in milliseconds (exponential moving average).
    // Raw frame times fluctuate wildly; a small EMA weight (0.05) keeps the
    // display readable without hiding real trends.
    float m_frame_time_ms  = 0.f;
    float m_fps            = 0.f;

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
    //
    // Initial parameters:
    //   yaw   = -90°  → camera positioned on the -Z side of the focus point,
    //                    matching the old eye=(50,80,-30), target=(50,0,50) view.
    //   pitch =  45°  → camera elevated 45° above the horizontal.
    //   distance=130  → far enough to see the entire 100×100-foot field.
    gfx::OrbitCamera m_camera{ -90.f, 45.f, 130.f };

    // Tracks whether a right-mouse drag is in progress.
    // Updated each frame inside handle_camera_input().
    bool  m_right_dragging = false;
    POINT m_drag_last{};

    // Cached by update_scene_constants() so that update_mouse_picking() can
    // unproject the cursor ray without recomputing camera matrices.
    //   m_inv_vp     — inverse VP matrix, NOT transposed (for CPU math)
    //   m_camera_eye — world-space eye position
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

        // Capture the QPC frequency once — it never changes after boot.
        QueryPerformanceFrequency(&m_qpc_frequency);
        // Seed the frame-start timestamp so the first frame's delta is near zero.
        QueryPerformanceCounter(&m_qpc_frame_start);

        // Create the default simulator. reset_field() seeds it from m_grass_field.
        m_sim = std::make_unique<sim::SimpleErosionSim>();

        // Delegate seeding to reset_field() so the Reset button and the
        // constructor share exactly the same initialisation logic.
        reset_field();
    }

    // ── Step 7: field reset ───────────────────────────────────────────────────

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
    // Algorithm:
    //   1. Convert window pixel (mouse_x, mouse_y) → NDC [-1,1]².
    //   2. Construct a far-clip point: float4(ndc, 1, 1).
    //   3. Multiply by the same inv_vp the shader uses → world-space position.
    //   4. Subtract the camera eye → unnormalised world-space direction.
    //   5. Solve for t where ray.y == 0 (the ground plane).
    //   6. Compute the column index from the hit XZ position.
    //
    // This is the CPU mirror of BuildCameraRay() + the ground-plane projection
    // in the HLSL pixel shader. No DDA loop needed: the terrain is
    // flat-bottomed, so projecting onto Y=0 is always the right first hit.
    void update_mouse_picking()
    {
        using namespace DirectX;

        m_hovered_column_valid = false;

        if (height == 0) return;

        // ── 1. Window pixel → NDC ─────────────────────────────────────────────
        // NDC x grows right, NDC y grows up.  Screen y grows downward, so flip.
        const float ndc_x =  (static_cast<float>(mouse_x) / static_cast<float>(width))  * 2.f - 1.f;
        const float ndc_y = -(static_cast<float>(mouse_y) / static_cast<float>(height)) * 2.f + 1.f;

        // ── 2. Unproject the far-clip point into world space ──────────────────
        // m_inv_vp and m_camera_eye are cached by update_scene_constants() which
        // runs just before this call, so they always reflect the current frame's
        // camera — including any orbit or zoom that happened last frame.
        XMVECTOR far_clip  = XMVectorSet(ndc_x, ndc_y, 1.f, 1.f);
        XMVECTOR far_world = XMVector4Transform(far_clip, XMLoadFloat4x4(&m_inv_vp));

        // Perspective divide: after the inverse projection w ≠ 1 in general.
        const float w_comp = XMVectorGetW(far_world);
        if (std::fabs(w_comp) < 1e-6f) return;
        far_world = XMVectorScale(far_world, 1.f / w_comp);

        // ── 3. Ray direction in world space ──────────────────────────────────
        XMFLOAT3 far_f3;
        XMStoreFloat3(&far_f3, far_world);

        const float dir_x = far_f3.x - m_camera_eye.x;
        const float dir_y = far_f3.y - m_camera_eye.y;
        const float dir_z = far_f3.z - m_camera_eye.z;

        // ── 4. Intersect with Y = 0 (the ground plane) ───────────────────────
        // Ray: P(t) = eye + t * dir.  Solve P.y = 0:  t = -eye.y / dir.y
        // If dir.y == 0 the ray is horizontal — it never hits the ground.
        // If t < 0 the intersection is behind the camera — discard.
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

        // ── Query adapter name and VRAM ───────────────────────────────────────
        // GetParent(IDXGIFactory) → EnumAdapters walks the list of GPUs.
        // We grab adapter 0 (same default as D3D12CreateDevice(nullptr)).
        // DXGI_ADAPTER_DESC1 gives us Description (wchar) and DedicatedVideoMemory.
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)))
            {
                DXGI_ADAPTER_DESC1 desc = {};
                if (SUCCEEDED(adapter->GetDesc1(&desc)))
                {
                    // Convert the wide Description string to narrow UTF-8 for ImGui.
                    const int needed = WideCharToMultiByte(
                        CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
                    m_adapter_name.resize(static_cast<std::size_t>(needed), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                        m_adapter_name.data(), needed, nullptr, nullptr);
                    // Remove null terminator that WideCharToMultiByte appends.
                    if (!m_adapter_name.empty() && m_adapter_name.back() == '\0')
                        m_adapter_name.pop_back();

                    m_adapter_vram_bytes = desc.DedicatedVideoMemory;
                }
            }
        }

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
        // Two slots: 0 = ImGui font atlas, 1 = field column heights SRV.
        // Both the terrain draw and ImGui share this one shader-visible heap.
        srv_desc.NumDescriptors = 2;
        srv_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(
            device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&imgui_srv_heap)),
            "CreateDescriptorHeap (SRV heap) failed.");

        // Cache the increment size so we can offset into the heap by slot index.
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

    // initialize_renderers() builds all available renderer objects and delegates
    // GPU resource creation to each one. All renderers are initialized upfront
    // so switching between them at runtime requires no GPU stall or re-initialization.
    //
    // To add a new renderer: implement IFieldRenderer, then push_back a new
    // instance here. The Renderer ImGui panel will automatically include it.
    void initialize_renderers()
    {
        gfx::RendererInitContext ctx {};
        ctx.device              = device.Get();
        ctx.rtv_format          = k_back_buffer_format;
        ctx.srv_descriptor_size = srv_descriptor_size;

        m_renderers.push_back(std::make_unique<gfx::RaycastRenderer>());
        m_renderers.push_back(std::make_unique<gfx::WireframeRenderer>());

        // Initialize every renderer now so switching is instant later.
        for (auto& r : m_renderers)
            r->initialize(ctx);
    }

    // initialize_field_buffer() creates the two upload heap buffers (heights
    // and constants), maps them persistently, and registers the field heights
    // SRV at descriptor heap slot 1.
    void initialize_field_buffer()
    {
        const int    cell_count     = m_sim->width() * m_sim->depth();
        const UINT64 field_buf_size = static_cast<UINT64>(cell_count) * sizeof(float);

        D3D12_HEAP_PROPERTIES upload_heap = {};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        // Template: raw buffer in upload heap.
        D3D12_RESOURCE_DESC buf_desc = {};
        buf_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Height             = 1;
        buf_desc.DepthOrArraySize   = 1;
        buf_desc.MipLevels          = 1;
        buf_desc.Format             = DXGI_FORMAT_UNKNOWN;  // required for buffers
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

        // {0,0} range = CPU-write-only; we never read back from this buffer.
        D3D12_RANGE no_read = { 0, 0 };
        throw_if_failed(
            m_field_buffer->Map(0, &no_read,
                reinterpret_cast<void**>(&m_field_buffer_mapped)),
            "Map (field heights) failed.");

        // ── Constant buffer ───────────────────────────────────────────────────
        // CBV GPU VA must be 256-byte aligned — buffer size rounded up to 256.
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
        // StructuredBuffer SRV: Format=UNKNOWN, StructureByteStride=sizeof(float).
        D3D12_CPU_DESCRIPTOR_HANDLE field_srv_cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        field_srv_cpu.ptr += srv_descriptor_size;  // slot 1 (slot 0 = ImGui font)

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement        = 0;
        srv_desc.Buffer.NumElements         = static_cast<UINT>(cell_count);
        srv_desc.Buffer.StructureByteStride = sizeof(float);
        srv_desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(m_field_buffer.Get(), &srv_desc, field_srv_cpu);

        // Prime the buffer so the very first frame draws real terrain.
        update_field_buffer();
    }

    // ── Step 8: per-frame camera input ───────────────────────────────────────

    // handle_camera_input() reads ImGui's mouse state and drives the orbit
    // camera. It must be called AFTER ImGui::NewFrame() so that ImGui's IO
    // structure is populated with fresh mouse position and button state.
    //
    // Key design decisions:
    //
    //   WantCaptureMouse gate — when the cursor is over an ImGui panel, ImGui
    //   "owns" the click. We respect that by skipping camera processing and
    //   clearing the drag flag. Without this gate, every click on an ImGui
    //   button would also rotate the camera.
    //
    //   ImGui IO vs raw Win32 — we read ImGui::IsMouseDown() / io.MouseWheel
    //   rather than raw Win32 WM_RBUTTONDOWN messages. This is cleaner: ImGui
    //   already translates raw Win32 messages into a polled state, and its
    //   MousePos is already in client-area coordinates. grass-field-002 uses
    //   the same pattern.
    //
    //   One-frame lag — handle_camera_input() runs after ImGui::NewFrame() but
    //   update_scene_constants() already ran this frame (before NewFrame). So
    //   camera changes take effect in the NEXT frame's constant buffer. At
    //   60 fps the lag is ~16 ms — imperceptible.
    void handle_camera_input()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Skip if ImGui is using the mouse (cursor over a panel, clicking a button).
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
        // Horizontal drag rotates yaw; vertical drag changes pitch.
        // The deltas are scaled to match grass-field-002's feel (0.35°/px yaw,
        // 0.25°/px pitch). Negating dx makes right-drag rotate clockwise.
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
        // io.MouseWheel is positive for scroll-up (zoom in) and negative for
        // scroll-down (zoom out). camera.zoom() adds to the orbit distance, so
        // we negate MouseWheel. Multiplier 1.2 matches grass-field-002.
        if (io.MouseWheel != 0.f)
            m_camera.zoom(-io.MouseWheel * 1.2f);
    }

    // ── Step 5: per-frame update methods ─────────────────────────────────────

    // update_scene_constants() computes the camera matrices for this frame
    // and writes them to the persistently-mapped constant buffer.
    //
    // Step 8 replaces the fixed camera with gfx::OrbitCamera. The orbit camera
    // stores (yaw, pitch, distance, focus). We convert those spherical
    // coordinates to a Cartesian eye position, then build the same RH view +
    // projection matrices the shader expects.
    //
    // DirectXMath matrix convention (important for the transpose):
    //   DX matrices are row-major. mul(vec, mat) in HLSL is row-vec × mat.
    //   HLSL cbuffer packing stores matrix columns sequentially.
    //   Result: we must transpose before upload so the HLSL mul() is correct.
    void update_scene_constants()
    {
        using namespace DirectX;

        const float aspect = (height > 0)
            ? static_cast<float>(width) / static_cast<float>(height)
            : 1.f;

        // ── Build eye position from orbit camera spherical coordinates ────────
        // OrbitCamera uses yaw (horizontal) and pitch (vertical elevation):
        //   eye.x = focus.x + distance * cos(yaw) * cos(pitch)
        //   eye.y = focus.y + distance * sin(pitch)
        //   eye.z = focus.z + distance * sin(yaw) * cos(pitch)
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

        // Right-handed coordinate system (same as the shader's world space).
        const XMMATRIX view   = XMMatrixLookAtRH(eye, target, up);
        const XMMATRIX proj   = XMMatrixPerspectiveFovRH(
            XMConvertToRadians(45.f), aspect, 0.1f, 500.f);

        const XMMATRIX vp     = XMMatrixMultiply(view, proj);
        const XMMATRIX inv_vp = XMMatrixInverse(nullptr, vp);

        // Cache for CPU-side mouse picking. Non-transposed: XMVector4Transform
        // treats its left operand as a row vector, which matches the shader's mul().
        XMStoreFloat4x4(&m_inv_vp, inv_vp);
        m_camera_eye = eye_f3;

        SceneConstants cb = {};

        // Transpose inv_vp before storing: DX row-major → HLSL column-major.
        XMStoreFloat4x4(&cb.inverse_view_projection, XMMatrixTranspose(inv_vp));
        XMStoreFloat4(&cb.camera_world_pos, eye);

        // Forward VP for mesh-based renderers (wireframe VS uses this to
        // project world-space vertices to clip space). Also transposed.
        XMStoreFloat4x4(&cb.view_projection, XMMatrixTranspose(vp));

        cb.field_origin_x  = 0.f;
        cb.field_origin_z  = 0.f;
        // GrassField is 100×100 columns at 1.0-foot voxels.
        cb.voxel_size_feet = m_grass_field.voxel_size_feet();
        // Max terrain height: ~7 ft for Perlin noise seeded terrain + margin.
        cb.max_height_feet = 12.f;
        cb.field_width     = static_cast<uint32_t>(m_sim->width());
        cb.field_depth     = static_cast<uint32_t>(m_sim->depth());

        memcpy(m_cb_buffer_mapped, &cb, sizeof(cb));
    }

    // update_field_buffer() converts the erosion field's integer inch-heights
    // to floating-point feet and writes them into the persistently-mapped GPU
    // upload buffer. Called every frame so any sim step appears immediately.
    void update_field_buffer()
    {
        const int w = m_sim->width();
        const int d = m_sim->depth();

        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
                m_field_buffer_mapped[z * w + x] =
                    m_sim->height_at(x, z) / 12.f;
    }

    // draw_field() delegates the terrain draw to whichever renderer is active.
    // It builds the per-frame argument struct and calls record_draw() on the
    // selected IFieldRenderer implementation.
    void draw_field()
    {
        // GPU handle for the field heights SRV (heap slot 1, after ImGui's slot 0).
        D3D12_GPU_DESCRIPTOR_HANDLE field_srv_gpu =
            imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();
        field_srv_gpu.ptr += srv_descriptor_size;  // slot 1

        gfx::RendererFrameArgs args {};
        args.cmd             = command_list.Get();
        args.srv_heap        = imgui_srv_heap.Get();
        args.cb_gpu_va       = m_cb_buffer->GetGPUVirtualAddress();
        args.field_srv_gpu   = field_srv_gpu;
        args.viewport_width  = width;
        args.viewport_height = height;
        args.field_width     = static_cast<UINT>(m_sim->width());
        args.field_depth     = static_cast<UINT>(m_sim->depth());

        m_renderers[static_cast<std::size_t>(m_active_renderer)]->record_draw(args);
    }

    // ── GPU synchronisation ───────────────────────────────────────────────────
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
    // tick_frame_time() measures the elapsed time since the last call and
    // updates m_frame_time_ms and m_fps via an exponential moving average (EMA).
    // Call once per frame at the top of the render loop.
    //
    // EMA formula: s_new = α * sample + (1 - α) * s_old
    //   α = 0.05 — smooth enough to be readable, responsive enough to track
    //   genuine frame-rate changes within a second or two.
    void tick_frame_time()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        // Elapsed ticks since last frame, converted to milliseconds.
        const double elapsed_ms = static_cast<double>(now.QuadPart - m_qpc_frame_start.QuadPart)
            / static_cast<double>(m_qpc_frequency.QuadPart) * 1000.0;
        m_qpc_frame_start = now;

        // Seed on the first non-zero sample to avoid the big initial spike.
        if (m_frame_time_ms == 0.f)
        {
            m_frame_time_ms = static_cast<float>(elapsed_ms);
        }
        else
        {
            constexpr float alpha = 0.05f;
            m_frame_time_ms = alpha * static_cast<float>(elapsed_ms)
                            + (1.f - alpha) * m_frame_time_ms;
        }

        m_fps = (m_frame_time_ms > 0.f) ? 1000.f / m_frame_time_ms : 0.f;
    }

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
        // handle_camera_input() must be called here — after ImGui::NewFrame() —
        // so ImGui's IO is populated with the current frame's mouse state.
        // It modifies m_camera; those changes take effect in the next frame's
        // update_scene_constants() call (one frame lag, imperceptible at 60 fps).
        handle_camera_input();

        // ── Renderer panel ────────────────────────────────────────────────────
        // SetNextWindowPos with FirstUseEver provides a sensible default if
        // imgui.ini has no saved position for this window (e.g., first run).
        ImGui::SetNextWindowPos(ImVec2(60, 240), ImGuiCond_FirstUseEver);
        ImGui::Begin("Renderer");

        // ── GPU identity ──────────────────────────────────────────────────────
        if (!m_adapter_name.empty())
            ImGui::Text("GPU:   %s", m_adapter_name.c_str());
        if (m_adapter_vram_bytes > 0)
        {
            // Show VRAM in MB (rounded) — more readable than raw bytes.
            const float vram_mb = static_cast<float>(m_adapter_vram_bytes) / (1024.f * 1024.f);
            ImGui::Text("VRAM:  %.0f MB", vram_mb);
        }

        ImGui::Separator();

        // ── Frame timing ──────────────────────────────────────────────────────
        // m_fps / m_frame_time_ms — QPC-based EMA, updated every frame.
        // ImGui::GetIO().Framerate — 120-frame smoothed average from ImGui.
        // Showing both: ImGui's value is steady for reading; QPC shows instant trend.
        ImGui::Text("FPS:   %.1f  (%.2f ms)", m_fps, m_frame_time_ms);
        ImGui::Text("       ImGui avg: %.1f fps", ImGui::GetIO().Framerate);

        ImGui::Separator();

        // ── Back buffer ───────────────────────────────────────────────────────
        ImGui::Text("Size:  %u x %u", width, height);
        ImGui::Text("Fmt:   R8G8B8A8_UNORM");   // k_back_buffer_format is DXGI_FORMAT_R8G8B8A8_UNORM
        ImGui::Text("Slot:  %u / %u", frame_index, k_frame_count);

        ImGui::Separator();

        // ── Renderer picker ───────────────────────────────────────────────────
        // All renderers are pre-initialized — switching never stalls the GPU.
        {
            const char* names[8] = {};
            const int   count    = static_cast<int>(
                std::min(m_renderers.size(), std::size_t{ 8 }));
            for (int i = 0; i < count; ++i)
                names[i] = m_renderers[static_cast<std::size_t>(i)]->name();

            ImGui::Combo("##renderer", &m_active_renderer, names, count);
        }

        ImGui::Separator();

        // Delegate renderer-specific settings to the active renderer.
        m_renderers[static_cast<std::size_t>(m_active_renderer)]->render_ui();

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
        ImGui::Begin("Simulation");

        ImGui::Text("Field:  %d x %d columns",
            m_sim->width(), m_sim->depth());
        ImGui::Text("Simulator: %s", m_sim->name());

        // Reset re-seeds the active simulator from the original GrassField
        // heights, discarding all accumulated simulation state.
        if (ImGui::Button("Reset"))
            reset_field();

        ImGui::Separator();

        // Delegate all simulator-specific UI to the simulator itself.
        // To add a new simulator type: implement IFieldSim, swap m_sim here,
        // call reset_field(). No other changes needed.
        m_sim->render_ui();

        ImGui::End();

        // ── Camera panel ──────────────────────────────────────────────────────
        //
        // Shows current orbit camera state and offers a Reset Camera button.
        // The reset re-constructs m_camera with the same default parameters
        // used at startup, snapping the view back to the initial overhead angle.
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
        //
        // Shows data for whichever column the cursor is hovering over.
        // update_mouse_picking() already ran this frame (called from run()),
        // so m_hovered_column_valid and m_hovered_x/z are already fresh.
        ImGui::Begin("Selected Column");
        if (m_hovered_column_valid)
        {
            const int h_inches = m_sim->height_at(m_hovered_x, m_hovered_z);
            ImGui::Text("Column [%d, %d]", m_hovered_x, m_hovered_z);
            ImGui::Text("Height: %d inches", h_inches);
            ImGui::Text("        %.2f feet",  static_cast<float>(h_inches) / 12.f);
        }
        else
        {
            // Cursor is outside the field or the ray is parallel to the ground.
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
            // Measure frame time before anything else so the sample is as accurate
            // as possible and represents the full previous frame.
            tick_frame_time();
            begin_frame();

            // Clear the back buffer. Even though the full-screen triangle
            // overwrites every pixel, clearing is fast and ensures no
            // stale pixels leak through if the PSO ever changes.
            {
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
                    rtv_heap->GetCPUDescriptorHandleForHeapStart();
                rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;
                command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
            }

            // Build camera matrices and write the constant buffer.
            // This also caches m_inv_vp and m_camera_eye for mouse picking.
            update_scene_constants();
            update_field_buffer();

            // Resolve which column the cursor is over. Runs AFTER
            // update_scene_constants() so it uses this frame's camera matrices.
            update_mouse_picking();

            // Draw the terrain (full-screen triangle + column raycast shader).
            // This runs before ImGui so the UI panels render on top.
            draw_field();

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            render_imgui();

            end_frame();
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

    case WM_MOUSEMOVE:
        // Track the cursor position so update_mouse_picking() can project
        // it into world space each frame. We store it regardless of whether
        // ImGui wants the mouse — the picking result is only *displayed* when
        // the cursor isn't over a UI panel, but we still need the coordinates.
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
        app.initialize_renderers();
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
