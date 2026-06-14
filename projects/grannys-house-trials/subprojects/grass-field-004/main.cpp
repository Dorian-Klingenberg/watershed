// grass-field-004 — Experiments: Pluggable Simulators + Renderers

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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
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

#include "sim/i_field_sim.h"
#include "sim/granny_map_visual_sim.h"
#include "grannys_house_trials/gfx/orbit_camera.h"

// ── Pluggable renderer (Step 9) ───────────────────────────────────────────────
#include "gfx/i_field_renderer.h"
#include "gfx/raycast_renderer.h"
#include "gfx/split_lod_renderer.h"
#include "gfx/wireframe_renderer.h"

#include <memory>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;
namespace sim = grannys_house_trials::sim;
namespace gfx = grannys_house_trials::gfx;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr wchar_t k_window_class_name[] = L"GrassField004Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 004 - Authored Granny Map";

// Double-buffering: two back buffers. While the GPU renders into frame N,
// the CPU can record commands for frame N+1. Using more buffers reduces
// stalls but increases memory; 2 is the standard starting point.
constexpr UINT k_frame_count = 2;

// The back-buffer pixel format. R8G8B8A8_UNORM is the most portable choice;
// every GPU and monitor chain supports it.
constexpr DXGI_FORMAT k_back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT k_depth_format = DXGI_FORMAT_D32_FLOAT;
constexpr int k_inches_per_foot = 12;

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
    int32_t  highlight_x;
    int32_t  highlight_z;

    // Forward VP matrix added for mesh-based renderers (wireframe, future passes).
    // The raycast PS uses inverse_view_projection; the wireframe VS uses this.
    // Declared at the end so the original 112-byte layout is unchanged for the
    // raycast shader which only reads the first 112 bytes from b0.
    DirectX::XMFLOAT4X4 view_projection;   // 64 bytes — total struct = 176 bytes
    // Upload buffer (k_cb_aligned_size) rounds to 256 bytes regardless.
};

constexpr UINT64 k_cb_aligned_size =
    (sizeof(SceneConstants) + 255u) & ~static_cast<UINT64>(255u);

struct FieldCellGpu
{
    float surface_height_feet = 0.f;
    float water_depth_feet    = 0.f;
    uint32_t material_id      = 0;
    uint32_t pad0             = 0;
};

struct SplitCoarseCellGpu
{
    float full_height_feet = 0.f;
    uint32_t material_id   = 0;
};

struct SplitFineCellGpu
{
    uint32_t fine_x           = 0;
    uint32_t fine_z           = 0;
    float    base_height_feet = 0.f;
    float    top_height_feet  = 0.f;
    float    water_depth_feet = 0.f;
    uint32_t material_id      = 0;
};

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
    ComPtr<ID3D12DescriptorHeap>      dsv_heap;
    ComPtr<ID3D12Resource>            depth_buffer;

    // The shared shader-visible CBV/SRV/UAV heap.
    // Slot 0: ImGui font atlas SRV  (set up by ImGui_ImplDX12_Init)
    // Slot 1: field column heights SRV  (set up in initialize_field_buffer)
    // Slot 2: split renderer coarse full-height SRV
    // Slot 3: split renderer compact fine-remainder SRV
    // Both the terrain draw and ImGui RenderDrawData bind this same heap,
    // so only one SetDescriptorHeaps call is needed per frame.
    ComPtr<ID3D12DescriptorHeap>      imgui_srv_heap;
    UINT                              rtv_descriptor_size  = 0;
    UINT                              dsv_descriptor_size  = 0;
    UINT                              srv_descriptor_size  = 0;  // cached in initialize_imgui
    bool                              m_resize_pending = false;
    UINT                              m_pending_width = 0;
    UINT                              m_pending_height = 0;

    // ── Step 9: pluggable renderers ───────────────────────────────────────────

    // All available renderers, initialized at startup. Switching between them is
    // instant because every renderer is already fully built (no GPU stall needed).
    // To add a new renderer: implement IFieldRenderer, push_back a new instance
    // in initialize_renderers(). Nothing else in Application changes.
    std::vector<std::unique_ptr<gfx::IFieldRenderer>> m_renderers;

    // Index into m_renderers for the currently active renderer.
    // Changed by the ImGui combo in the Renderer panel.
    int m_active_renderer = 1;

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

    // Upload heap buffer for field cells: visible surface height plus optional
    // water depth, both in feet. It is persistently mapped, but only rewritten
    // when simulator state changes; camera-only frames reuse the existing data.
    ComPtr<ID3D12Resource>            m_field_buffer;
    FieldCellGpu*                     m_field_buffer_mapped = nullptr;
    bool                              m_field_buffer_dirty = true;

    // Renderer-only split LOD data. The simulation remains one-inch; these
    // buffers summarize it for the coarse/fine two-pass renderer.
    ComPtr<ID3D12Resource>            m_split_coarse_buffer;
    SplitCoarseCellGpu*               m_split_coarse_mapped = nullptr;
    UINT                              m_split_coarse_width = 0;
    UINT                              m_split_coarse_depth = 0;

    ComPtr<ID3D12Resource>            m_split_fine_buffer;
    SplitFineCellGpu*                 m_split_fine_mapped = nullptr;
    UINT                              m_split_fine_capacity = 0;
    UINT                              m_split_fine_count = 0;
    bool                              m_split_lod_dirty = true;

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

    // Cell selected by left-click. The camera orbits around this cell's visible
    // surface; when no cell is selected, it orbits around the field center.
    int  m_selected_x = 0;
    int  m_selected_z = 0;
    bool m_selected_column_valid = false;

    // Column currently marked as "would be selected on right-click".
    int  m_highlight_x = -1;
    int  m_highlight_z = -1;
    bool m_highlight_valid = false;

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
    gfx::OrbitCamera m_camera{ -90.f, 45.f, 190.f };

    // Input gesture state:
    // - middle-drag orbits
    // - right-drag pans
    // - right-click (without drag) selects hovered column
    bool  m_middle_orbit_dragging = false;
    bool  m_right_pan_dragging = false;
    POINT m_middle_drag_last{};
    POINT m_right_drag_last{};
    POINT m_right_press_start{};
    float m_right_pan_plane_y = 0.0f;

    float m_focus_offset_x = 0.0f;
    float m_focus_offset_z = 0.0f;

    // Cached by update_scene_constants() so that update_mouse_picking() can
    // unproject the cursor ray without recomputing camera matrices.
    //   m_inv_vp     — inverse VP matrix, NOT transposed (for CPU math)
    //   m_camera_eye — world-space eye position
    DirectX::XMFLOAT4X4  m_inv_vp{};
    DirectX::XMFLOAT3    m_camera_eye{};

    // ── Construction / destruction ───────────────────────────────────────────

    std::vector<std::unique_ptr<sim::IFieldSim>> m_sims;
    int m_active_sim = 0;

    Application()
    {
        g_app = this;

        // Capture the QPC frequency once — it never changes after boot.
        QueryPerformanceFrequency(&m_qpc_frequency);
        // Seed the frame-start timestamp so the first frame's delta is near zero.
        QueryPerformanceCounter(&m_qpc_frame_start);

        initialize_simulators();
        reset_all_simulators();
    }

    // ── Simulator experiments ────────────────────────────────────────────────

    void initialize_simulators()
    {
        m_sims.push_back(std::make_unique<sim::GrannyMapVisualSim>());
    }

    sim::IFieldSim& active_sim()
    {
        if (m_sims.empty())
            throw std::logic_error("Application: no simulator experiments registered.");

        const int last_index = static_cast<int>(m_sims.size()) - 1;
        m_active_sim = std::clamp(m_active_sim, 0, last_index);
        return *m_sims[static_cast<std::size_t>(m_active_sim)];
    }

    gfx::IFieldRenderer& active_renderer()
    {
        if (m_renderers.empty())
            throw std::logic_error("Application: no field renderers registered.");

        const int last_index = static_cast<int>(m_renderers.size()) - 1;
        m_active_renderer = std::clamp(m_active_renderer, 0, last_index);
        return *m_renderers[static_cast<std::size_t>(m_active_renderer)];
    }

    void reset_simulator(sim::IFieldSim& sim)
    {
        sim.reset(simulation_grid_width(), simulation_grid_depth(), {});
    }

    void reset_all_simulators()
    {
        for (auto& sim_ptr : m_sims)
            reset_simulator(*sim_ptr);

        m_field_buffer_dirty = true;
    }

    // Reset only the visible experiment, preserving the other experiments'
    // state so switching between them feels like moving between workbenches.
    void reset_field()
    {
        reset_simulator(active_sim());
        m_field_buffer_dirty = true;
    }

    [[nodiscard]] int simulation_grid_width() const noexcept
    {
        return sim::GrannyMapVisualSim::fine_wide;
    }

    [[nodiscard]] int simulation_grid_depth() const noexcept
    {
        return sim::GrannyMapVisualSim::fine_deep;
    }

    [[nodiscard]] float simulation_voxel_size_feet() const noexcept
    {
        return 1.0f / static_cast<float>(sim::GrannyMapVisualSim::inches_per_foot);
    }

    struct CpuRay
    {
        float ox = 0.0f;
        float oy = 0.0f;
        float oz = 0.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        float dz = 0.0f;
    };

    [[nodiscard]] bool build_mouse_ray(int px, int py, CpuRay& out_ray) const
    {
        using namespace DirectX;

        if (width == 0 || height == 0)
            return false;

        const float ndc_x = (static_cast<float>(px) / static_cast<float>(width)) * 2.0f - 1.0f;
        const float ndc_y = -(static_cast<float>(py) / static_cast<float>(height)) * 2.0f + 1.0f;

        XMVECTOR far_clip = XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f);
        XMVECTOR far_world = XMVector4Transform(far_clip, XMLoadFloat4x4(&m_inv_vp));
        const float w_comp = XMVectorGetW(far_world);
        if (std::fabs(w_comp) < 1e-6f)
            return false;
        far_world = XMVectorScale(far_world, 1.0f / w_comp);

        DirectX::XMFLOAT3 far_f3{};
        XMStoreFloat3(&far_f3, far_world);

        const float dir_x = far_f3.x - m_camera_eye.x;
        const float dir_y = far_f3.y - m_camera_eye.y;
        const float dir_z = far_f3.z - m_camera_eye.z;
        const float len_sq = dir_x * dir_x + dir_y * dir_y + dir_z * dir_z;
        if (len_sq < 1e-10f)
            return false;

        const float inv_len = 1.0f / std::sqrt(len_sq);
        out_ray.ox = m_camera_eye.x;
        out_ray.oy = m_camera_eye.y;
        out_ray.oz = m_camera_eye.z;
        out_ray.dx = dir_x * inv_len;
        out_ray.dy = dir_y * inv_len;
        out_ray.dz = dir_z * inv_len;
        return true;
    }

    [[nodiscard]] static bool intersect_horizontal_plane(
        const CpuRay& ray,
        float plane_y,
        float& out_x,
        float& out_z)
    {
        if (std::fabs(ray.dy) < 1e-6f)
            return false;
        const float t = (plane_y - ray.oy) / ray.dy;
        if (t < 0.0f)
            return false;
        out_x = ray.ox + ray.dx * t;
        out_z = ray.oz + ray.dz * t;
        return true;
    }

    // ── Step 7: mouse picking ─────────────────────────────────────────────────

    // Uses a DDA traversal through X/Z columns, with per-column AABB ray tests.
    // This matches angled views much better than projecting to a fixed ground
    // plane, because it can hit side walls and elevated tops directly.
    void update_mouse_picking()
    {
        m_hovered_column_valid = false;

        CpuRay ray{};
        if (!build_mouse_ray(mouse_x, mouse_y, ray))
            return;

        const sim::IFieldSim& sim = active_sim();
        const int field_w = sim.width();
        const int field_d = sim.depth();
        const float voxel = simulation_voxel_size_feet();
        const float field_max_x = static_cast<float>(field_w) * voxel;
        const float field_max_z = static_cast<float>(field_d) * voxel;
        constexpr float field_max_y = 24.0f;

        auto slab_axis = [](float ro, float rd, float mn, float mx, float& t0, float& t1) -> bool
        {
            if (std::fabs(rd) < 1e-6f)
                return ro >= mn && ro <= mx;
            const float inv = 1.0f / rd;
            float a = (mn - ro) * inv;
            float b = (mx - ro) * inv;
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            return t1 >= t0;
        };

        float t_enter = 0.0f;
        float t_exit = 1e30f;
        if (!slab_axis(ray.ox, ray.dx, 0.0f, field_max_x, t_enter, t_exit)) return;
        if (!slab_axis(ray.oy, ray.dy, 0.0f, field_max_y, t_enter, t_exit)) return;
        if (!slab_axis(ray.oz, ray.dz, 0.0f, field_max_z, t_enter, t_exit)) return;
        if (t_exit < 0.0f) return;
        t_enter = std::max(t_enter, 0.0f);

        const float start_x = ray.ox + ray.dx * t_enter;
        const float start_z = ray.oz + ray.dz * t_enter;

        int cx = static_cast<int>(std::floor(start_x / voxel));
        int cz = static_cast<int>(std::floor(start_z / voxel));
        cx = std::clamp(cx, 0, field_w - 1);
        cz = std::clamp(cz, 0, field_d - 1);

        const int step_x = (ray.dx > 0.0f) ? 1 : ((ray.dx < 0.0f) ? -1 : 0);
        const int step_z = (ray.dz > 0.0f) ? 1 : ((ray.dz < 0.0f) ? -1 : 0);

        const float inf = 1e30f;
        const float t_delta_x = (step_x != 0) ? (voxel / std::fabs(ray.dx)) : inf;
        const float t_delta_z = (step_z != 0) ? (voxel / std::fabs(ray.dz)) : inf;

        const float next_boundary_x = (step_x > 0)
            ? (static_cast<float>(cx + 1) * voxel)
            : (static_cast<float>(cx) * voxel);
        const float next_boundary_z = (step_z > 0)
            ? (static_cast<float>(cz + 1) * voxel)
            : (static_cast<float>(cz) * voxel);

        float t_max_x = (step_x != 0) ? ((next_boundary_x - start_x) / ray.dx) : inf;
        float t_max_z = (step_z != 0) ? ((next_boundary_z - start_z) / ray.dz) : inf;
        if (t_max_x < 0.0f) t_max_x = 0.0f;
        if (t_max_z < 0.0f) t_max_z = 0.0f;

        float seg_t = t_enter;

        while (cx >= 0 && cx < field_w && cz >= 0 && cz < field_d && seg_t <= t_exit)
        {
            const float top_y = static_cast<float>(
                sim.height_at(cx, cz) + sim.water_depth_at(cx, cz)) / 12.0f;

            if (top_y > 0.0f)
            {
                float local_t0 = seg_t;
                float local_t1 = std::min(t_exit, seg_t + std::min(t_max_x, t_max_z));

                const float cell_min_x = static_cast<float>(cx) * voxel;
                const float cell_max_x = cell_min_x + voxel;
                const float cell_min_z = static_cast<float>(cz) * voxel;
                const float cell_max_z = cell_min_z + voxel;

                if (slab_axis(ray.ox, ray.dx, cell_min_x, cell_max_x, local_t0, local_t1) &&
                    slab_axis(ray.oy, ray.dy, 0.0f, top_y, local_t0, local_t1) &&
                    slab_axis(ray.oz, ray.dz, cell_min_z, cell_max_z, local_t0, local_t1))
                {
                    if (local_t1 >= std::max(local_t0, 0.0f))
                    {
                        m_hovered_x = cx;
                        m_hovered_z = cz;
                        m_hovered_column_valid = true;
                        return;
                    }
                }
            }

            if (t_max_x < t_max_z)
            {
                seg_t += t_max_x;
                t_max_z -= t_max_x;
                t_max_x = t_delta_x;
                cx += step_x;
            }
            else
            {
                seg_t += t_max_z;
                t_max_x -= t_max_z;
                t_max_z = t_delta_z;
                cz += step_z;
            }
        }
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
        if (m_split_coarse_mapped)
        {
            m_split_coarse_buffer->Unmap(0, nullptr);
            m_split_coarse_mapped = nullptr;
        }
        if (m_split_fine_mapped)
        {
            m_split_fine_buffer->Unmap(0, nullptr);
            m_split_fine_mapped = nullptr;
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

        // ── 7. Depth Buffer ──────────────────────────────────────────────────
        //
        // The split coarse/fine renderer uses real depth testing so its two
        // mesh passes compose correctly. Existing renderers leave depth off.
        {
            D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
            dsv_heap_desc.NumDescriptors = 1;
            dsv_heap_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            throw_if_failed(
                device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)),
                "CreateDescriptorHeap (DSV) failed.");

            dsv_descriptor_size = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

            D3D12_HEAP_PROPERTIES heap_props = {};
            heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC depth_desc = {};
            depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depth_desc.Width = width;
            depth_desc.Height = height;
            depth_desc.DepthOrArraySize = 1;
            depth_desc.MipLevels = 1;
            depth_desc.Format = k_depth_format;
            depth_desc.SampleDesc.Count = 1;
            depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE clear_value = {};
            clear_value.Format = k_depth_format;
            clear_value.DepthStencil.Depth = 1.0f;
            clear_value.DepthStencil.Stencil = 0;

            throw_if_failed(
                device->CreateCommittedResource(
                    &heap_props, D3D12_HEAP_FLAG_NONE, &depth_desc,
                    D3D12_RESOURCE_STATE_DEPTH_WRITE,
                    &clear_value, IID_PPV_ARGS(&depth_buffer)),
                "CreateCommittedResource (depth buffer) failed.");

            D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
            dsv_desc.Format = k_depth_format;
            dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
            device->CreateDepthStencilView(
                depth_buffer.Get(), &dsv_desc,
                dsv_heap->GetCPUDescriptorHandleForHeapStart());
        }

        // ── 8. Command Allocators ────────────────────────────────────────────
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

        // ── 9. Command List ──────────────────────────────────────────────────
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

        // ── 10. Fence + Event ────────────────────────────────────────────────
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
        // Slots:
        //   0 = ImGui font atlas
        //   1 = field column heights SRV
        //   2 = split renderer coarse SRV
        //   3 = split renderer fine-remainder SRV
        // Terrain draws and ImGui share this one shader-visible heap.
        srv_desc.NumDescriptors = 4;
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
        ctx.dsv_format          = k_depth_format;
        ctx.srv_descriptor_size = srv_descriptor_size;

        m_renderers.push_back(std::make_unique<gfx::RaycastRenderer>());
        m_renderers.push_back(std::make_unique<gfx::SplitLodRenderer>());
        m_renderers.push_back(std::make_unique<gfx::WireframeRenderer>());

        // Initialize every renderer now so switching is instant later.
        for (auto& r : m_renderers)
            r->initialize(ctx);
    }

    // initialize_field_buffer() creates the upload heap buffers (field cells,
    // split-renderer summaries, and constants), maps them persistently, and
    // registers their SRVs in the shared descriptor heap.
    void initialize_field_buffer()
    {
        const int    cell_count     = active_sim().width() * active_sim().depth();
        const UINT64 field_buf_size = static_cast<UINT64>(cell_count) * sizeof(FieldCellGpu);

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

        // ── Split LOD renderer buffers ───────────────────────────────────────
        const int detail = sim::GrannyMapVisualSim::inches_per_foot;
        m_split_coarse_width = static_cast<UINT>((active_sim().width() + detail - 1) / detail);
        m_split_coarse_depth = static_cast<UINT>((active_sim().depth() + detail - 1) / detail);
        const UINT split_coarse_count = m_split_coarse_width * m_split_coarse_depth;
        m_split_fine_capacity = static_cast<UINT>(cell_count);

        buf_desc.Width = static_cast<UINT64>(split_coarse_count) * sizeof(SplitCoarseCellGpu);
        throw_if_failed(
            device->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&m_split_coarse_buffer)),
            "CreateCommittedResource (split coarse buffer) failed.");

        throw_if_failed(
            m_split_coarse_buffer->Map(0, &no_read,
                reinterpret_cast<void**>(&m_split_coarse_mapped)),
            "Map (split coarse buffer) failed.");

        buf_desc.Width = static_cast<UINT64>(m_split_fine_capacity) * sizeof(SplitFineCellGpu);
        throw_if_failed(
            device->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&m_split_fine_buffer)),
            "CreateCommittedResource (split fine buffer) failed.");

        throw_if_failed(
            m_split_fine_buffer->Map(0, &no_read,
                reinterpret_cast<void**>(&m_split_fine_mapped)),
            "Map (split fine buffer) failed.");

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
        // StructuredBuffer SRV: Format=UNKNOWN, StructureByteStride=sizeof(FieldCellGpu).
        D3D12_CPU_DESCRIPTOR_HANDLE field_srv_cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        field_srv_cpu.ptr += srv_descriptor_size;  // slot 1 (slot 0 = ImGui font)

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement        = 0;
        srv_desc.Buffer.NumElements         = static_cast<UINT>(cell_count);
        srv_desc.Buffer.StructureByteStride = sizeof(FieldCellGpu);
        srv_desc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(m_field_buffer.Get(), &srv_desc, field_srv_cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE split_coarse_srv_cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        split_coarse_srv_cpu.ptr += srv_descriptor_size * 2; // slot 2

        srv_desc.Buffer.NumElements         = split_coarse_count;
        srv_desc.Buffer.StructureByteStride = sizeof(SplitCoarseCellGpu);
        device->CreateShaderResourceView(
            m_split_coarse_buffer.Get(), &srv_desc, split_coarse_srv_cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE split_fine_srv_cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        split_fine_srv_cpu.ptr += srv_descriptor_size * 3; // slot 3

        srv_desc.Buffer.NumElements         = m_split_fine_capacity;
        srv_desc.Buffer.StructureByteStride = sizeof(SplitFineCellGpu);
        device->CreateShaderResourceView(
            m_split_fine_buffer.Get(), &srv_desc, split_fine_srv_cpu);

        // Prime the buffer so the very first frame draws real terrain.
        m_field_buffer_dirty = true;
        m_split_lod_dirty = true;
        update_field_buffer();
    }

    // ── Step 8: per-frame camera input ───────────────────────────────────────

    DirectX::XMFLOAT3 camera_focus_point()
    {
        const float voxel = simulation_voxel_size_feet();

        if (m_selected_column_valid)
        {
            const sim::IFieldSim& sim = active_sim();
            if (m_selected_x >= 0 && m_selected_x < sim.width() &&
                m_selected_z >= 0 && m_selected_z < sim.depth())
            {
                return DirectX::XMFLOAT3{
                    (static_cast<float>(m_selected_x) + 0.5f) * voxel + m_focus_offset_x,
                    static_cast<float>(sim.height_at(m_selected_x, m_selected_z)) / 12.f,
                    (static_cast<float>(m_selected_z) + 0.5f) * voxel + m_focus_offset_z
                };
            }

            m_selected_column_valid = false;
        }

        const sim::IFieldSim& sim = active_sim();
        return DirectX::XMFLOAT3{
            static_cast<float>(sim.width()) * voxel * 0.5f + m_focus_offset_x,
            0.5f,
            static_cast<float>(sim.depth()) * voxel * 0.5f + m_focus_offset_z
        };
    }

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
            m_middle_orbit_dragging = false;
            m_right_pan_dragging = false;
            return;
        }

        const POINT cur_pos{
            static_cast<LONG>(io.MousePos.x),
            static_cast<LONG>(io.MousePos.y)
        };

        m_highlight_valid = m_hovered_column_valid;
        if (m_highlight_valid)
        {
            m_highlight_x = m_hovered_x;
            m_highlight_z = m_hovered_z;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_hovered_column_valid)
        {
            m_selected_x = m_hovered_x;
            m_selected_z = m_hovered_z;
            m_selected_column_valid = true;
        }

        // Middle-drag: orbit.
        const bool middle_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (middle_down)
        {
            if (m_middle_orbit_dragging)
            {
                const int dx = cur_pos.x - m_middle_drag_last.x;
                const int dy = cur_pos.y - m_middle_drag_last.y;
                m_camera.orbit(
                    static_cast<float>(-dx) * 0.35f,
                    static_cast<float>( dy) * 0.25f);
            }
            m_middle_drag_last = cur_pos;
            m_middle_orbit_dragging = true;
        }
        else
        {
            m_middle_orbit_dragging = false;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_right_press_start = cur_pos;
            m_right_drag_last = cur_pos;
            m_right_pan_dragging = false;
            m_right_pan_plane_y = 0.0f;
            if (m_selected_column_valid)
            {
                const sim::IFieldSim& sim = active_sim();
                if (m_selected_x >= 0 && m_selected_x < sim.width() &&
                    m_selected_z >= 0 && m_selected_z < sim.depth())
                {
                    m_right_pan_plane_y = static_cast<float>(
                        sim.height_at(m_selected_x, m_selected_z) +
                        sim.water_depth_at(m_selected_x, m_selected_z)) / 12.0f;
                }
            }
        }

        const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (right_down)
        {
            const int total_dx = cur_pos.x - m_right_press_start.x;
            const int total_dy = cur_pos.y - m_right_press_start.y;
            const int drag_threshold_px = 3;
            if (!m_right_pan_dragging &&
                (std::abs(total_dx) > drag_threshold_px ||
                 std::abs(total_dy) > drag_threshold_px))
            {
                m_right_pan_dragging = true;
            }

            if (m_right_pan_dragging)
            {
                CpuRay prev_ray{};
                CpuRay cur_ray{};
                if (build_mouse_ray(m_right_drag_last.x, m_right_drag_last.y, prev_ray) &&
                    build_mouse_ray(cur_pos.x, cur_pos.y, cur_ray))
                {
                    float prev_x = 0.0f;
                    float prev_z = 0.0f;
                    float cur_x = 0.0f;
                    float cur_z = 0.0f;
                    if (intersect_horizontal_plane(prev_ray, m_right_pan_plane_y, prev_x, prev_z) &&
                        intersect_horizontal_plane(cur_ray, m_right_pan_plane_y, cur_x, cur_z))
                    {
                        m_focus_offset_x -= (cur_x - prev_x);
                        m_focus_offset_z -= (cur_z - prev_z);
                    }
                }
            }

            m_right_drag_last = cur_pos;
        }
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            m_right_pan_dragging = false;
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
    // Step 8 replaces the fixed camera with gfx::OrbitCamera. The camera stores
    // yaw, pitch, and distance; this experiment chooses the focus point from the
    // selected cell, falling back to the field center. We convert those spherical
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

        const XMFLOAT3 focus_f = camera_focus_point();
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
        // Expand each authored 1-foot cell into 12 one-inch render/sim cells.
        cb.voxel_size_feet = simulation_voxel_size_feet();
        // Max terrain height plus enough headroom for fluid experiments.
        cb.max_height_feet = 24.f;
        cb.field_width     = static_cast<uint32_t>(active_sim().width());
        cb.field_depth     = static_cast<uint32_t>(active_sim().depth());
        cb.highlight_x     = m_highlight_valid ? m_highlight_x : -1;
        cb.highlight_z     = m_highlight_valid ? m_highlight_z : -1;

        memcpy(m_cb_buffer_mapped, &cb, sizeof(cb));
    }

    // update_field_buffer() converts the active simulator's inch values to
    // floating-point feet and writes them into the persistently-mapped GPU
    // upload buffer, but only after sim data changed.
    void update_field_buffer()
    {
        if (!m_field_buffer_dirty)
            return;

        sim::IFieldSim& sim = active_sim();
        const int w = sim.width();
        const int d = sim.depth();

        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
            {
                FieldCellGpu& cell = m_field_buffer_mapped[z * w + x];
                cell.surface_height_feet = sim.height_at(x, z) / 12.f;
                cell.water_depth_feet = sim.water_depth_at(x, z) / 12.f;
                cell.material_id = sim.material_id_at(x, z);
            }

        m_split_lod_dirty = true;
        m_field_buffer_dirty = false;
    }

    void update_split_lod_buffers(sim::IFieldSim& sim, int w, int d)
    {
        constexpr int detail = sim::GrannyMapVisualSim::inches_per_foot;
        m_split_fine_count = 0;

        for (UINT coarse_z = 0; coarse_z < m_split_coarse_depth; ++coarse_z)
        {
            for (UINT coarse_x = 0; coarse_x < m_split_coarse_width; ++coarse_x)
            {
                int min_terrain_inches = std::numeric_limits<int>::max();

                const int start_x = static_cast<int>(coarse_x) * detail;
                const int start_z = static_cast<int>(coarse_z) * detail;
                const int end_x = std::min(start_x + detail, w);
                const int end_z = std::min(start_z + detail, d);

                for (int z = start_z; z < end_z; ++z)
                {
                    for (int x = start_x; x < end_x; ++x)
                    {
                        const int surface_inches = sim.height_at(x, z);
                        const int water_inches = sim.water_depth_at(x, z);
                        min_terrain_inches = std::min(min_terrain_inches, surface_inches - water_inches);
                    }
                }

                if (min_terrain_inches == std::numeric_limits<int>::max())
                    min_terrain_inches = 0;

                const int full_base_inches = std::max(
                    0, (min_terrain_inches / k_inches_per_foot) * k_inches_per_foot);
                const UINT coarse_i = coarse_z * m_split_coarse_width + coarse_x;
                m_split_coarse_mapped[coarse_i].full_height_feet =
                    static_cast<float>(full_base_inches) / static_cast<float>(k_inches_per_foot);
                m_split_coarse_mapped[coarse_i].material_id =
                    sim.material_id_at(start_x, start_z);

                for (int z = start_z; z < end_z; ++z)
                {
                    for (int x = start_x; x < end_x; ++x)
                    {
                        const int surface_inches = sim.height_at(x, z);
                        const int water_inches = sim.water_depth_at(x, z);
                        if (surface_inches <= full_base_inches)
                            continue;

                        if (m_split_fine_count >= m_split_fine_capacity)
                            continue;

                        SplitFineCellGpu& fine = m_split_fine_mapped[m_split_fine_count++];
                        fine.fine_x = static_cast<uint32_t>(x);
                        fine.fine_z = static_cast<uint32_t>(z);
                        fine.base_height_feet =
                            static_cast<float>(full_base_inches) / static_cast<float>(k_inches_per_foot);
                        fine.top_height_feet =
                            static_cast<float>(surface_inches) / static_cast<float>(k_inches_per_foot);
                        fine.water_depth_feet =
                            static_cast<float>(water_inches) / static_cast<float>(k_inches_per_foot);
                        fine.material_id = sim.material_id_at(x, z);
                    }
                }
            }
        }
    }

    // draw_field() delegates the terrain draw to whichever renderer is active.
    // It builds the per-frame argument struct and calls record_draw() on the
    // selected IFieldRenderer implementation.
    void draw_field()
    {
        gfx::IFieldRenderer& renderer = active_renderer();
        if (renderer.uses_split_lod_buffers() && m_split_lod_dirty)
        {
            sim::IFieldSim& sim = active_sim();
            update_split_lod_buffers(sim, sim.width(), sim.depth());
            m_split_lod_dirty = false;
        }

        // GPU handle for the field heights SRV (heap slot 1, after ImGui's slot 0).
        D3D12_GPU_DESCRIPTOR_HANDLE field_srv_gpu =
            imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();
        field_srv_gpu.ptr += srv_descriptor_size;  // slot 1

        D3D12_GPU_DESCRIPTOR_HANDLE split_lod_srv_gpu =
            imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();
        split_lod_srv_gpu.ptr += srv_descriptor_size * 2; // slot 2, followed by slot 3

        gfx::RendererFrameArgs args {};
        args.cmd             = command_list.Get();
        args.srv_heap        = imgui_srv_heap.Get();
        args.cb_gpu_va       = m_cb_buffer->GetGPUVirtualAddress();
        args.field_srv_gpu   = field_srv_gpu;
        args.split_lod_srv_gpu = split_lod_srv_gpu;
        args.viewport_width  = width;
        args.viewport_height = height;
        args.field_width     = static_cast<UINT>(active_sim().width());
        args.field_depth     = static_cast<UINT>(active_sim().depth());
        args.split_coarse_width = m_split_coarse_width;
        args.split_coarse_depth = m_split_coarse_depth;
        args.split_fine_count   = m_split_fine_count;

        renderer.record_draw(args);
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

        if (active_renderer().uses_depth_buffer())
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
                dsv_heap->GetCPUDescriptorHandleForHeapStart();
            command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
        }
        else
        {
            command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        }
    }

    void handle_pending_resize()
    {
        if (!m_resize_pending)
            return;
        if (m_pending_width == 0 || m_pending_height == 0)
            return;

        wait_for_gpu();

        for (UINT i = 0; i < k_frame_count; ++i)
            render_targets[i].Reset();
        depth_buffer.Reset();

        throw_if_failed(
            swap_chain->ResizeBuffers(
                k_frame_count,
                m_pending_width,
                m_pending_height,
                k_back_buffer_format,
                0),
            "ResizeBuffers failed.");

        width = m_pending_width;
        height = m_pending_height;
        frame_index = swap_chain->GetCurrentBackBufferIndex();

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < k_frame_count; ++i)
        {
            throw_if_failed(
                swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i])),
                "GetBuffer (resize) failed.");
            device->CreateRenderTargetView(render_targets[i].Get(), nullptr, rtv);
            rtv.ptr += rtv_descriptor_size;
        }

        D3D12_RESOURCE_DESC depth_desc = {};
        depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_desc.Width = width;
        depth_desc.Height = height;
        depth_desc.DepthOrArraySize = 1;
        depth_desc.MipLevels = 1;
        depth_desc.Format = k_depth_format;
        depth_desc.SampleDesc.Count = 1;
        depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depth_clear = {};
        depth_clear.Format = k_depth_format;
        depth_clear.DepthStencil.Depth = 1.0f;
        depth_clear.DepthStencil.Stencil = 0;

        D3D12_HEAP_PROPERTIES depth_heap = {};
        depth_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        depth_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        depth_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        depth_heap.CreationNodeMask = 1;
        depth_heap.VisibleNodeMask = 1;

        throw_if_failed(
            device->CreateCommittedResource(
                &depth_heap,
                D3D12_HEAP_FLAG_NONE,
                &depth_desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &depth_clear,
                IID_PPV_ARGS(&depth_buffer)),
            "CreateCommittedResource (resize depth) failed.");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = k_depth_format;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            depth_buffer.Get(),
            &dsv_desc,
            dsv_heap->GetCPUDescriptorHandleForHeapStart());

        m_resize_pending = false;
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

            const int previous_renderer = m_active_renderer;
            ImGui::Combo("##renderer", &m_active_renderer, names, count);
            if (m_active_renderer != previous_renderer &&
                active_renderer().uses_split_lod_buffers())
            {
                m_split_lod_dirty = true;
            }
        }

        ImGui::Separator();

        // Delegate renderer-specific settings to the active renderer.
        active_renderer().render_ui();

        ImGui::End();

        // ── Simulation panel ──────────────────────────────────────────────────
        //
        // The panel has two parts: experiment selection owned by Application,
        // then simulator-specific controls delegated to the active IFieldSim.
        ImGui::Begin("Simulation");

        sim::IFieldSim& current_sim = active_sim();

        ImGui::Text("Field:  %d x %d columns",
            current_sim.width(), current_sim.depth());
        ImGui::Text("Scale:  %.0f in / cell", simulation_voxel_size_feet() * k_inches_per_foot);
        ImGui::Text("Experiment: %s", current_sim.name());

        {
            const char* names[8] = {};
            const int count = static_cast<int>(
                std::min(m_sims.size(), std::size_t{ 8 }));
            for (int i = 0; i < count; ++i)
                names[i] = m_sims[static_cast<std::size_t>(i)]->name();

            const int previous_active_sim = m_active_sim;
            ImGui::Combo("##simulator", &m_active_sim, names, count);
            if (m_active_sim != previous_active_sim)
                m_field_buffer_dirty = true;
        }

        // Reset Active re-seeds only the visible experiment. Reset All is useful
        // when comparing experiments from the same starting terrain.
        if (ImGui::Button("Reset Active"))
            reset_field();

        ImGui::SameLine();

        if (ImGui::Button("Reset All"))
            reset_all_simulators();

        ImGui::Separator();

        // Delegate all simulator-specific UI to the simulator itself.
        if (active_sim().render_ui())
            m_field_buffer_dirty = true;

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
        if (m_selected_column_valid)
            ImGui::Text("Orbit:    [%d, %d]", m_selected_x, m_selected_z);
        else
            ImGui::TextDisabled("Orbit: field center");
        ImGui::Separator();
        ImGui::TextDisabled("Left-click a cell to set focus");
        ImGui::TextDisabled("Middle-drag to orbit");
        ImGui::TextDisabled("Right-drag to pan");
        ImGui::TextDisabled("Scroll to zoom");
        if (ImGui::Button("Reset Camera"))
        {
            m_camera = gfx::OrbitCamera{ -90.f, 45.f, 190.f };
            m_focus_offset_x = 0.0f;
            m_focus_offset_z = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Focus"))
            m_selected_column_valid = false;
        ImGui::End();

        // ── Selected column panel ─────────────────────────────────────────────
        //
        // Shows data for the left-click selected cell. If nothing is selected
        // yet, it falls back to the current hover result.
        ImGui::Begin("Selected Column");
        const bool has_column = m_selected_column_valid || m_hovered_column_valid;
        const int column_x = m_selected_column_valid ? m_selected_x : m_hovered_x;
        const int column_z = m_selected_column_valid ? m_selected_z : m_hovered_z;

        if (has_column)
        {
            const int h_inches = active_sim().height_at(column_x, column_z);
            const int water_inches = active_sim().water_depth_at(column_x, column_z);
            ImGui::Text("%s [%d, %d]",
                m_selected_column_valid ? "Selected" : "Hovered",
                column_x, column_z);
            ImGui::Text("Surface: %d inches", h_inches);
            ImGui::Text("        %.2f feet",  static_cast<float>(h_inches) / 12.f);
            ImGui::Text("Material: %s", active_sim().material_name_at(column_x, column_z));
            ImGui::Text("Foot cell: [%d, %d]", column_x / k_inches_per_foot, column_z / k_inches_per_foot);
            if (water_inches > 0)
                ImGui::Text("Water:  %d inches", water_inches);
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

            handle_pending_resize();
            if (width == 0 || height == 0)
                continue;

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

                if (active_renderer().uses_depth_buffer())
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
                        dsv_heap->GetCPUDescriptorHandleForHeapStart();
                    command_list->ClearDepthStencilView(
                        dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                }
            }

            // Build camera matrices and write the constant buffer.
            // This also caches m_inv_vp and m_camera_eye for mouse picking.
            update_scene_constants();
            if (m_field_buffer_dirty)
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
        if (g_app && wparam != SIZE_MINIMIZED)
        {
            g_app->m_pending_width = static_cast<UINT>(LOWORD(lparam));
            g_app->m_pending_height = static_cast<UINT>(HIWORD(lparam));
            g_app->m_resize_pending = true;
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
        MessageBoxA(nullptr, e.what(), "Grass Field 004 - Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
