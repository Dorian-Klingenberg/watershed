// grass-field-003 — Experiments: Pluggable Simulators + Renderers

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

#include "grannys_house_trials/sim/grass_field.h"
#include "sim/i_field_sim.h"
#include "sim/simple_cellular_fluid_sim.h"
#include "sim/simple_cellular_fluid_gpu_phase1_sim.h"
#include "sim/simple_cellular_fluid_sim_active_sources.h"
#include "sim/simple_cellular_fluid_sim_parallel.h"
#include "sim/simple_cellular_fluid_sim_round1.h"
#include "sim/simple_hydraulic_erosion_rain_sim.h"
#include "sim/simple_obrien_volume_flow_sim.h"
#include "sim/simple_pipe_flux_shallow_water_sim.h"
#include "sim/simple_shallow_water_sim.h"
#include "sim/simple_slosh_basin_flow_sim.h"
#include "sim/simple_slosh_mac_sim.h"
#include "sim/simple_terrain_head_pipe_flow_sim.h"
#include "sim/simple_virtual_pipe_fluid_sim.h"
#include "sim/simple_erosion_sim.h"
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
constexpr wchar_t k_window_class_name[] = L"GrassField003Window";
constexpr wchar_t k_window_title[]      = L"Grass Field 003 — Experiments";

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
};

struct SplitCoarseCellGpu
{
    float full_height_feet = 0.f;
    float pad0             = 0.f;
};

struct SplitFineCellGpu
{
    uint32_t fine_x                     = 0;
    uint32_t fine_z                     = 0;
    float    base_height_feet           = 0.f;
    float    top_height_feet            = 0.f;
    float    water_depth_feet           = 0.f;
    uint32_t water_side_mask            = 0xFu;
    float    velocity_feet_per_second   = 0.f;
    float    sediment_depth_feet        = 0.f;
    float    terrain_delta_feet         = 0.f;
};

enum class LessonOptionKind
{
    Simulation,
    Renderer
};

struct ExperimentLessonUnit
{
    const char* display_name = "";
    const char* group = "";
    const char* lesson_file = "";
    const char* overview = "";
    const char* comparison = "";
    LessonOptionKind option_kind = LessonOptionKind::Simulation;
    int primary_option = 0;
    int alternate_option = 0;
    const char* primary_label = "";
    const char* alternate_label = "";
};

// CPU lessons lead the catalog. Non-CPU lesson titles follow alphabetical order:
// HLSL, Raycast, Split LOD, Wireframe.
constexpr std::array<ExperimentLessonUnit, 18> k_experiment_lessons = {{
    {
        "CPU 01 - Simple Erosion Simulator", "CPU Simulations",
        "lesson_experiment_simple_erosion_sim.md",
        "Introduces the original CPU terrain-changing experiment and the simulator interface.",
        "Compare erosion, which changes terrain, with fluid baseline, which adds a visible water surface.",
        LessonOptionKind::Simulation, 0, 1, "Show Erosion", "Show Fluid Baseline"
    },
    {
        "CPU 02 - Cellular Fluid Baseline", "CPU Simulations",
        "lesson_experiment_cellular_fluid_sim.md",
        "Introduces four-neighbor cellular water flow as the preserved reference implementation.",
        "Compare the trusted fluid baseline with Round 1, which should compute the same result with less overhead.",
        LessonOptionKind::Simulation, 1, 2, "Show Fluid Baseline", "Show Round 1"
    },
    {
        "CPU 03 - Fluid Upgrade / Round 1", "CPU Simulations",
        "lesson_experiment_cellular_fluid_sim_upgrade.md",
        "Explains the cone diagnosis, fractional water repair, clock, and first semantics-preserving optimization round.",
        "Compare Baseline and Round 1 with the same inputs; Round 1 measured 1.281x faster in both CPU benchmark scenarios.",
        LessonOptionKind::Simulation, 2, 1, "Show Round 1", "Show Baseline"
    },
    {
        "CPU 04 - Active Sources / Round 2", "CPU Simulations",
        "lesson_experiment_cellular_fluid_active_sources.md",
        "Skips flow evaluation for dry sources while retaining ordered contributions and full-field application.",
        "Compare Round 2 against Round 1: active sources win for a local pour but lose under uniform rain.",
        LessonOptionKind::Simulation, 3, 2, "Show Round 2", "Show Round 1"
    },
    {
        "CPU 05 - Deterministic Parallel", "CPU Simulations",
        "lesson_experiment_cellular_fluid_cpu_parallel.md",
        "Computes source proposals in parallel and replays deltas serially to preserve accumulation order.",
        "Compare CPU Parallel against Round 1; it passed exact state validation but measured slower in both workloads.",
        LessonOptionKind::Simulation, 4, 2, "Show CPU Parallel", "Show Round 1"
    },
    {
        "CPU 06 - Shallow Water Heightfield", "CPU Simulations",
        "lesson_experiment_shallow_water_heightfield_cpu_basic.md",
        "Starts a new CPU-only water experiment with depth plus horizontal velocity.",
        "Compare shallow water against Round 1 cellular flow: velocity adds directional memory, but this is a new model.",
        LessonOptionKind::Simulation, 5, 2, "Show Shallow Water", "Show Round 1"
    },
    {
        "CPU 07 - Virtual Pipe Fluid", "CPU Simulations",
        "lesson_experiment_virtual_pipe_fluid_sim.md",
        "Adapts the O'Brien/Hodgins virtual-pipe volume model into a CPU heightfield experiment with eight-neighbor pipes.",
        "Compare virtual pipes with shallow water: both have memory, but pipes store signed tunnel flow instead of cell velocity.",
        LessonOptionKind::Simulation, 6, 5, "Show Virtual Pipes", "Show Shallow Water"
    },
    {
        "CPU 08 - Pipe-Flux Shallow Water", "CPU Simulations",
        "lesson_experiment_pipe_flux_shallow_water_sim.md",
        "Combines shallow-water depth with persistent edge flux so water momentum lives in the pipes between cells.",
        "Compare pipe-flux shallow water with virtual pipes: both use edge flow, but this branch is the stable shallow-water path.",
        LessonOptionKind::Simulation, 7, 6, "Show Pipe-Flux", "Show Virtual Pipes"
    },
    {
        "CPU 09 - O'Brien Volume Flow", "CPU Simulations",
        "lesson_experiment_obrien_volume_flow_sim.md",
        "Recreates only the O'Brien/Hodgins main volume-flow subsystem: columns, eight virtual pipes, hydrostatic pressure, and positivity correction.",
        "Compare the paper-faithful volume subsystem with the earlier virtual-pipe branch that added game-feel controls.",
        LessonOptionKind::Simulation, 8, 6, "Show Paper Flow", "Show Virtual Pipes"
    },
    {
        "CPU 10 - Terrain-Head Pipe Flow", "CPU Simulations",
        "lesson_experiment_terrain_head_pipe_flow_sim.md",
        "Adds terrain bed elevation back to the pipe-flow question so water pools by free-surface height instead of spreading as a terrain-blind sheet.",
        "Compare terrain-head flow with the paper-only volume subsystem: this branch should respect hills and collect in depressions.",
        LessonOptionKind::Simulation, 9, 8, "Show Terrain Head", "Show Paper Flow"
    },
    {
        "CPU 11 - Hydraulic Erosion + Rainfall", "CPU Simulations",
        "lesson_experiment_hydraulic_erosion_rain_sim.md",
        "Combines terrain-head pipe water, suspended sediment transport, terrain mutation, and stochastic falling rain drops on the erosion valley map.",
        "Compare hydraulic erosion with terrain-head flow: the water path is inherited, but this branch cuts and deposits terrain while rain arrives as visible drops.",
        LessonOptionKind::Simulation, 10, 9, "Show Hydraulic Erosion", "Show Terrain Head"
    },
    {
        "CPU 12 - Slosh Basin Flow", "CPU Simulations",
        "lesson_experiment_slosh_basin_flow_sim.md",
        "Returns to fixed-terrain pre-erosion flow on a purpose-built basin map with shelves, an island, a baffle, and a spillway.",
        "Compare slosh basin flow with terrain-head flow: the solver is inherited for now, but the map is designed for rebound and wave experiments.",
        LessonOptionKind::Simulation, 11, 9, "Show Slosh Basin", "Show Terrain Head"
    },
    {
        "CPU 13 - Slosh MAC Grid", "CPU Simulations",
        "lesson_experiment_slosh_mac_grid_sim.md",
        "Staggered MAC grid solver: u and v velocity components live on cell faces and carry genuine inertia between steps.",
        "Compare MAC grid with pipe slosh: the MAC solver retains momentum across the basin so waves arrive at rigid obstacles at full speed.",
        LessonOptionKind::Simulation, 12, 11, "Show MAC Grid", "Show Pipe Slosh"
    },
    {
        "HLSL Compute Phase 1", "Other Experiments",
        "lesson_experiment_cellular_fluid_hlsl_compute_phase1.md",
        "Runs the cellular fluid proposal/apply passes on a compute queue and reads water back for the existing renderer.",
        "Compare HLSL Phase 1 with the CPU baseline while its standalone benchmark evaluates GPU timing and state difference.",
        LessonOptionKind::Simulation, 12, 1, "Show HLSL Phase 1", "Show CPU Baseline"
    },
    {
        "HLSL Compute Phase 2 - Tiled Resident", "Other Experiments",
        "lesson_experiment_cellular_fluid_hlsl_compute_phase2_tiled_resident.md",
        "Loads 16 by 16 surface tiles with one-cell halos into group-shared memory and keeps water resident on the GPU.",
        "Compare tiled resident Phase 2 with readback-backed Phase 1 while preserving both shader experiments.",
        LessonOptionKind::Simulation, 13, 12, "Show Tiled Resident", "Show Phase 1"
    },
    {
        "Raycast Renderer", "Other Experiments",
        "lesson_experiment_raycast_renderer.md",
        "Packages the full-screen column raycast technique as a pluggable renderer.",
        "Compare direct raycast display with the mixed Split LOD renderer on the same active simulation.",
        LessonOptionKind::Renderer, 0, 1, "Show Raycast", "Show Split LOD"
    },
    {
        "Split LOD Renderer", "Other Experiments",
        "lesson_experiment_split_lod_renderer.md",
        "Displays coarse and fine column information together and remains the default mixed rendering mode.",
        "Compare mixed Split LOD display against the Raycast renderer using identical simulation state.",
        LessonOptionKind::Renderer, 1, 0, "Show Split LOD", "Show Raycast"
    },
    {
        "Wireframe Renderer", "Other Experiments",
        "lesson_experiment_wireframe_renderer.md",
        "Shows the column field as geometry, making structure and height transitions easier to inspect.",
        "Compare wireframe structure with the mixed Split LOD surface presentation.",
        LessonOptionKind::Renderer, 2, 1, "Show Wireframe", "Show Split LOD"
    }
}};

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
    UINT width  = 1600;
    UINT height = 900;

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
    // Slot 4: GPU-resident fluid terrain SRV
    // Slot 5: GPU-resident fluid water SRV
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
    int m_active_renderer = 1; // Split Coarse/Fine Columns is the mixed default.

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
    double m_frame_elapsed_seconds = 0.0;

    // Simulation playback advances several ordinary cellular ticks per rendered
    // frame. This keeps the deterministic one-step rule intact while letting
    // the now-smooth GPU path spend its frame budget on more fluid updates.
    static constexpr int k_simulation_steps_per_render_frame = 10;
    bool m_simulation_running = false;

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

    // Column currently marked as "would be selected on right-click".
    int  m_highlight_x = -1;
    int  m_highlight_z = -1;
    bool m_highlight_valid = false;

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
    //   m_vp         — forward VP matrix, NOT transposed (for CPU overlays)
    //   m_camera_eye — world-space eye position
    DirectX::XMFLOAT4X4  m_inv_vp{};
    DirectX::XMFLOAT4X4  m_vp{};
    DirectX::XMFLOAT3    m_camera_eye{};
    std::vector<sim::FieldVisualPoint> m_sim_visual_points;

    // ── Construction / destruction ───────────────────────────────────────────

    // GrassField provides the initial terrain heights used to seed every
    // simulator experiment. Rendering, picking, and GPU upload go through
    // IFieldSim so the app can switch experiments without changing draw code.
    sim::GrassField                    m_grass_field{100, 100, 1.0f};
    std::vector<std::unique_ptr<sim::IFieldSim>> m_sims;
    int m_active_sim = 2; // Optimized Round 1 fluid is the working default.
    int m_active_lesson = 0;
    int m_lesson_view = 0; // 0 = overview, 1 = compare.

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
        m_sims.push_back(std::make_unique<sim::SimpleErosionSim>());
        m_sims.push_back(std::make_unique<sim::SimpleCellularFluidSim>());
        m_sims.push_back(std::make_unique<sim::SimpleCellularFluidSimRound1>());
        m_sims.push_back(std::make_unique<sim::SimpleCellularFluidSimActiveSources>());
        m_sims.push_back(std::make_unique<sim::SimpleCellularFluidSimParallel>());
        m_sims.push_back(std::make_unique<sim::SimpleShallowWaterSim>());
        m_sims.push_back(std::make_unique<sim::SimpleVirtualPipeFluidSim>());
        m_sims.push_back(std::make_unique<sim::SimplePipeFluxShallowWaterSim>());
        m_sims.push_back(std::make_unique<sim::SimpleObrienVolumeFlowSim>());
        m_sims.push_back(std::make_unique<sim::SimpleTerrainHeadPipeFlowSim>());
        m_sims.push_back(std::make_unique<sim::SimpleHydraulicErosionRainSim>());
        m_sims.push_back(std::make_unique<sim::SimpleSloshBasinFlowSim>());
        m_sims.push_back(std::make_unique<sim::SimpleSloshMacSim>());
    }

    // GPU-backed simulations are registered only after the D3D12 device and
    // compiled shader blobs are available. CPU modes remain the startup default.
    void initialize_gpu_simulators()
    {
        auto gpu_sim = std::make_unique<sim::SimpleCellularFluidGpuPhase1Sim>(device.Get());
        reset_simulator(*gpu_sim);
        m_sims.push_back(std::move(gpu_sim));

        auto tiled_resident_sim =
            std::make_unique<sim::SimpleCellularFluidGpuPhase1Sim>(device.Get(), true);
        reset_simulator(*tiled_resident_sim);
        m_sims.push_back(std::move(tiled_resident_sim));
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

    [[nodiscard]] bool cell_in_bounds(
        const sim::IFieldSim& sim, int x, int z) const noexcept
    {
        return x >= 0 && x < sim.width() && z >= 0 && z < sim.depth();
    }

    [[nodiscard]] bool selected_cell_valid_for(
        const sim::IFieldSim& sim) const noexcept
    {
        return m_selected_column_valid && cell_in_bounds(sim, m_selected_x, m_selected_z);
    }

    [[nodiscard]] bool uses_coarse_grass_seed(const sim::IFieldSim& sim) const noexcept
    {
        return sim.cell_size_feet() >= m_grass_field.voxel_size_feet() * 0.999f;
    }

    [[nodiscard]] static const char* seed_map_profile_name(
        sim::SeedMapProfile profile) noexcept
    {
        switch (profile)
        {
        case sim::SeedMapProfile::SharedGrassField:
            return "Shared GrassField";
        case sim::SeedMapProfile::ErosionInclineValleys:
            return "Erosion Incline + Valleys";
        case sim::SeedMapProfile::SloshBasin:
            return "Slosh Basin";
        }

        return "Unknown";
    }

    [[nodiscard]] static float distance_to_segment_feet(
        float x,
        float z,
        float ax,
        float az,
        float bx,
        float bz) noexcept
    {
        const float vx = bx - ax;
        const float vz = bz - az;
        const float wx = x - ax;
        const float wz = z - az;
        const float len_sq = vx * vx + vz * vz;
        const float t = len_sq > 0.0001f
            ? std::clamp((wx * vx + wz * vz) / len_sq, 0.0f, 1.0f)
            : 0.0f;
        const float px = ax + vx * t;
        const float pz = az + vz * t;
        const float dx = x - px;
        const float dz = z - pz;
        return std::sqrt(dx * dx + dz * dz);
    }

    [[nodiscard]] static int terrain_noise_inches(int x, int z) noexcept
    {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u;
        h ^= static_cast<std::uint32_t>(z) * 668265263u;
        h = (h ^ (h >> 13u)) * 1274126177u;
        h ^= h >> 16u;
        return static_cast<int>(h % 7u) - 3;
    }

    [[nodiscard]] std::vector<int> build_erosion_incline_valley_seed(
        const sim::IFieldSim& target_sim) const
    {
        const int w = simulation_grid_width(target_sim);
        const int d = simulation_grid_depth(target_sim);
        const float cell_size_feet = target_sim.cell_size_feet();
        const float width_feet = static_cast<float>(w) * cell_size_feet;
        const float depth_feet = static_cast<float>(d) * cell_size_feet;

        std::vector<int> heights;
        heights.reserve(static_cast<std::size_t>(w * d));

        for (int z = 0; z < d; ++z)
        {
            const float world_z = (static_cast<float>(z) + 0.5f) * cell_size_feet;
            const float zn = depth_feet > 0.0f ? world_z / depth_feet : 0.0f;

            for (int x = 0; x < w; ++x)
            {
                const float world_x =
                    (static_cast<float>(x) + 0.5f) * cell_size_feet;
                const float xn = width_feet > 0.0f ? world_x / width_feet : 0.0f;

                float terrain_height =
                    178.0f - 112.0f * zn + 10.0f * (xn - 0.5f);

                const float main_center_x =
                    width_feet * (0.46f + 0.08f * std::sin(zn * 10.0f));
                const float main_dist = std::abs(world_x - main_center_x);
                terrain_height -= 30.0f *
                    std::exp(-(main_dist * main_dist) / (2.0f * 3.6f * 3.6f));

                const float west_tributary = distance_to_segment_feet(
                    world_x, world_z,
                    width_feet * 0.16f, depth_feet * 0.20f,
                    width_feet * 0.44f, depth_feet * 0.58f);
                terrain_height -= 18.0f *
                    std::exp(-(west_tributary * west_tributary) /
                        (2.0f * 2.8f * 2.8f));

                const float east_tributary = distance_to_segment_feet(
                    world_x, world_z,
                    width_feet * 0.82f, depth_feet * 0.30f,
                    width_feet * 0.54f, depth_feet * 0.66f);
                terrain_height -= 16.0f *
                    std::exp(-(east_tributary * east_tributary) /
                        (2.0f * 2.4f * 2.4f));

                const float left_ridge = distance_to_segment_feet(
                    world_x, world_z,
                    width_feet * 0.24f, depth_feet * 0.06f,
                    width_feet * 0.30f, depth_feet * 0.88f);
                terrain_height += 14.0f *
                    std::exp(-(left_ridge * left_ridge) /
                        (2.0f * 4.0f * 4.0f));

                const float right_ridge = distance_to_segment_feet(
                    world_x, world_z,
                    width_feet * 0.72f, depth_feet * 0.08f,
                    width_feet * 0.66f, depth_feet * 0.78f);
                terrain_height += 12.0f *
                    std::exp(-(right_ridge * right_ridge) /
                        (2.0f * 4.4f * 4.4f));

                const float basin_x =
                    (world_x - width_feet * 0.54f) / (width_feet * 0.21f);
                const float basin_z =
                    (world_z - depth_feet * 0.82f) / (depth_feet * 0.12f);
                const float basin_dist = basin_x * basin_x + basin_z * basin_z;
                if (basin_dist < 1.0f)
                    terrain_height -= 28.0f * (1.0f - basin_dist);

                terrain_height +=
                    3.0f * std::sin(world_x * 0.55f + world_z * 0.11f);
                terrain_height += 2.0f * std::sin(world_z * 0.38f + 1.7f);
                terrain_height += static_cast<float>(terrain_noise_inches(x / 4, z / 4));

                heights.push_back(
                    std::max(4, static_cast<int>(std::lround(terrain_height))));
            }
        }

        return heights;
    }

    [[nodiscard]] std::vector<int> build_slosh_basin_seed(
        const sim::IFieldSim& target_sim) const
    {
        const int w = simulation_grid_width(target_sim);
        const int d = simulation_grid_depth(target_sim);

        // All heights are hard step functions — no Gaussian blending.
        // Every feature has a vertical face so water hits a cliff, not a ramp.
        //
        // Layout on a 100×100 grid (1 cell = 1 foot):
        //   Floor:          16 in — flat open basin
        //   Rim:            72 in — 4-cell-wide border wall on all sides
        //   Spillway:       50 in — notch in the south rim (x 74..88); above typical water level
        //   Central pillar: 72 in — 16×14 foot solid block at x[43..58], z[43..56]
        //   East wall:      72 in — 3-foot-wide partial wall x[67..69], z[4..72]
        //                          (gap z[73..95] lets water flow around south end)
        //   Diagonal baffle:72 in — rasterized 2-cell-wide line from [22,76] to [60,28]
        //   West ledge:     34 in — half-height step x[4..20], z[34..66]
        //                          (water surges over when deep enough, drains back)

        constexpr int k_floor    = 16;
        constexpr int k_wall     = 72;
        constexpr int k_spillway = 50;  // above typical water surface; reached only by large waves
        constexpr int k_ledge    = 34;
        constexpr int k_rim      = 4;  // cells from edge that are wall

        // Baffle: rasterized line segment from (22,76) to (60,28), 2-cell half-width.
        constexpr float k_baffle_x0 = 22.0f;
        constexpr float k_baffle_z0 = 76.0f;
        constexpr float k_baffle_x1 = 60.0f;
        constexpr float k_baffle_z1 = 28.0f;
        constexpr float k_baffle_half_width = 1.8f;

        std::vector<int> heights;
        heights.reserve(static_cast<std::size_t>(w * d));

        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                int h = k_floor;

                // Rim — 4-cell border wall on all sides.
                const bool on_rim = (x < k_rim || x >= w - k_rim ||
                                     z < k_rim || z >= d - k_rim);
                if (on_rim)
                {
                    h = k_wall;
                    // Spillway notch in the south rim (z = last 4 rows, x 74..88).
                    if (z >= d - k_rim && x >= 74 && x <= 88)
                        h = k_spillway;
                }

                // Central solid pillar — 16×14 foot block.
                if (x >= 43 && x <= 58 && z >= 43 && z <= 56)
                    h = k_wall;

                // East partial wall — 3 cells wide, stops at z=72 leaving a south gap.
                if (x >= 67 && x <= 69 && z >= k_rim && z <= 72)
                    h = k_wall;

                // West half-height ledge — water can surge onto this and drain back.
                if (x >= k_rim && x <= 20 && z >= 34 && z <= 66)
                    h = std::max(h, k_ledge);

                // Diagonal baffle — rasterized 2-cell-wide line segment.
                {
                    const float fx = static_cast<float>(x) + 0.5f;
                    const float fz = static_cast<float>(z) + 0.5f;
                    const float dist = distance_to_segment_feet(
                        fx, fz,
                        k_baffle_x0, k_baffle_z0,
                        k_baffle_x1, k_baffle_z1);
                    if (dist < k_baffle_half_width)
                        h = k_wall;
                }

                heights.push_back(h);
            }
        }

        return heights;
    }

    [[nodiscard]] std::vector<int> build_seed_heights(const sim::IFieldSim& target_sim) const
    {
        if (target_sim.seed_map_profile() == sim::SeedMapProfile::ErosionInclineValleys)
            return build_erosion_incline_valley_seed(target_sim);
        if (target_sim.seed_map_profile() == sim::SeedMapProfile::SloshBasin)
            return build_slosh_basin_seed(target_sim);

        const int source_w = m_grass_field.width();
        const int source_d = m_grass_field.depth();
        const int detail = sim::GrassField::detail_patch_resolution();

        if (uses_coarse_grass_seed(target_sim))
        {
            std::vector<int> heights;
            heights.reserve(static_cast<std::size_t>(source_w * source_d));

            for (int z = 0; z < source_d; ++z)
                for (int x = 0; x < source_w; ++x)
                    heights.push_back(m_grass_field.coarse_top_height_inches_at(x, z));

            return heights;
        }

        std::vector<int> heights;
        heights.reserve(static_cast<std::size_t>(source_w * detail * source_d * detail));

        for (int source_z = 0; source_z < source_d; ++source_z)
        {
            for (int local_z = 0; local_z < detail; ++local_z)
            {
                for (int source_x = 0; source_x < source_w; ++source_x)
                {
                    for (int local_x = 0; local_x < detail; ++local_x)
                    {
                        heights.push_back(m_grass_field.fine_top_height_inches_at(
                            source_x, source_z, local_x, local_z));
                    }
                }
            }
        }

        return heights;
    }

    void reset_simulator(sim::IFieldSim& sim)
    {
        sim.reset(simulation_grid_width(sim), simulation_grid_depth(sim), build_seed_heights(sim));
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

    void select_simulator_option(int option_index)
    {
        if (option_index < 0 || option_index >= static_cast<int>(m_sims.size()))
            return;

        if (m_active_sim != option_index)
        {
            m_active_sim = option_index;
            m_field_buffer_dirty = true;
            if (active_sim().has_gpu_resident_field())
                m_active_renderer = 3; // GPU Resident Fluid Raycast
            else if (active_renderer().uses_gpu_resident_fluid_buffers())
                m_active_renderer = 1; // Restore the mixed CPU-facing renderer.
        }
    }

    void select_renderer_option(int option_index)
    {
        if (option_index < 0 || option_index >= static_cast<int>(m_renderers.size()))
            return;

        if (m_active_renderer != option_index)
        {
            m_active_renderer = option_index;
            if (active_renderer().uses_gpu_resident_fluid_buffers() &&
                !active_sim().has_gpu_resident_field() &&
                m_sims.size() > 13)
            {
                m_active_sim = 13; // Matching tiled resident HLSL simulation.
                m_field_buffer_dirty = true;
            }
            if (active_renderer().uses_split_lod_buffers())
                m_split_lod_dirty = true;
        }
    }

    [[nodiscard]] int simulation_grid_width(const sim::IFieldSim& sim) const noexcept
    {
        if (uses_coarse_grass_seed(sim))
            return m_grass_field.width();

        return m_grass_field.width() * sim::GrassField::detail_patch_resolution();
    }

    [[nodiscard]] int simulation_grid_depth(const sim::IFieldSim& sim) const noexcept
    {
        if (uses_coarse_grass_seed(sim))
            return m_grass_field.depth();

        return m_grass_field.depth() * sim::GrassField::detail_patch_resolution();
    }

    [[nodiscard]] float simulation_voxel_size_feet()
    {
        return active_sim().cell_size_feet();
    }

    [[nodiscard]] int split_lod_group_size_cells(const sim::IFieldSim& sim) const noexcept
    {
        const float cells_per_foot = m_grass_field.voxel_size_feet() / sim.cell_size_feet();
        return std::max(1, static_cast<int>(std::lround(cells_per_foot)));
    }

    [[nodiscard]] uint32_t split_lod_water_side_mask(
        const sim::IFieldSim& sim, int x, int z) const
    {
        constexpr uint32_t k_neg_z_side = 1u << 0u;
        constexpr uint32_t k_pos_z_side = 1u << 1u;
        constexpr uint32_t k_neg_x_side = 1u << 2u;
        constexpr uint32_t k_pos_x_side = 1u << 3u;
        constexpr float k_water_threshold_inches = 0.006f;

        if (sim.water_depth_inches_at(x, z) <= k_water_threshold_inches)
            return k_neg_z_side | k_pos_z_side | k_neg_x_side | k_pos_x_side;

        const auto is_wet_neighbor =
            [&sim](int nx, int nz)
            {
                if (nx < 0 || nx >= sim.width() || nz < 0 || nz >= sim.depth())
                    return false;

                return sim.water_depth_inches_at(nx, nz) > k_water_threshold_inches;
            };

        uint32_t mask = 0u;
        if (!is_wet_neighbor(x, z - 1)) mask |= k_neg_z_side;
        if (!is_wet_neighbor(x, z + 1)) mask |= k_pos_z_side;
        if (!is_wet_neighbor(x - 1, z)) mask |= k_neg_x_side;
        if (!is_wet_neighbor(x + 1, z)) mask |= k_pos_x_side;
        return mask;
    }

    // ── Step 7: mouse picking ─────────────────────────────────────────────────

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

    // DDA traversal through X/Z columns, with per-column AABB ray tests.
    // Matches angled views better than a flat ground-plane projection because
    // it can hit side walls and elevated tops directly.
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
        //   4 = GPU-resident terrain SRV
        //   5 = GPU-resident water SRV
        // Terrain draws and ImGui share this one shader-visible heap.
        srv_desc.NumDescriptors = 6;
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
        m_renderers.push_back(std::make_unique<gfx::RaycastRenderer>(true));

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
        const int detail = split_lod_group_size_cells(active_sim());
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
                    sim.surface_height_inches_at(m_selected_x, m_selected_z) / 12.f,
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

        // Right: record press start; threshold check converts to pan; release selects.
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
                    m_right_pan_plane_y = sim.surface_height_inches_at(
                        m_selected_x, m_selected_z) / 12.0f;
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
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !m_right_pan_dragging)
        {
            if (m_hovered_column_valid)
            {
                m_selected_x = m_hovered_x;
                m_selected_z = m_hovered_z;
                m_selected_column_valid = true;
            }
            m_right_pan_dragging = false;
        }
        else if (!right_down)
        {
            m_right_pan_dragging = false;
        }

        // ── Scroll wheel: zoom ────────────────────────────────────────────────
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
        XMStoreFloat4x4(&m_vp, vp);
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

    // Draw transient simulator-owned markers, such as falling rain drops, as a
    // lightweight ImGui overlay instead of encoding them into terrain columns.
    void draw_sim_visual_points(const sim::IFieldSim& sim)
    {
        m_sim_visual_points.clear();
        sim.append_visual_points(m_sim_visual_points);
        if (m_sim_visual_points.empty() || width == 0 || height == 0)
            return;

        using namespace DirectX;

        const XMMATRIX vp = XMLoadFloat4x4(&m_vp);
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

        const auto channel =
            [](float value) -> int
            {
                return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

        for (const sim::FieldVisualPoint& point : m_sim_visual_points)
        {
            const XMVECTOR world =
                XMVectorSet(point.x_feet, point.y_feet, point.z_feet, 1.0f);
            const XMVECTOR clip = XMVector4Transform(world, vp);
            const float clip_w = XMVectorGetW(clip);
            if (clip_w <= 0.01f)
                continue;

            const float ndc_x = XMVectorGetX(clip) / clip_w;
            const float ndc_y = XMVectorGetY(clip) / clip_w;
            if (ndc_x < -1.15f || ndc_x > 1.15f ||
                ndc_y < -1.15f || ndc_y > 1.15f)
            {
                continue;
            }

            const float screen_x =
                (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
            const float screen_y =
                (-ndc_y * 0.5f + 0.5f) * static_cast<float>(height);
            const float radius =
                std::clamp(point.radius_pixels, 1.0f, 8.0f);
            const ImU32 color = IM_COL32(
                channel(point.r), channel(point.g), channel(point.b), channel(point.a));
            const ImU32 streak_color = IM_COL32(
                channel(point.r), channel(point.g), channel(point.b), channel(point.a * 0.35f));

            draw_list->AddLine(
                ImVec2(screen_x, screen_y - radius * 3.0f),
                ImVec2(screen_x, screen_y + radius),
                streak_color,
                1.2f);
            draw_list->AddCircleFilled(
                ImVec2(screen_x, screen_y), radius, color, 10);
        }
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
                cell.surface_height_feet = sim.surface_height_inches_at(x, z) / 12.f;
                cell.water_depth_feet = sim.water_depth_inches_at(x, z) / 12.f;
            }

        m_split_lod_dirty = true;
        m_field_buffer_dirty = false;
    }

    void update_gpu_resident_descriptors()
    {
        sim::IFieldSim& sim = active_sim();
        if (!sim.has_gpu_resident_field())
            return;

        const UINT element_count = static_cast<UINT>(sim.width() * sim.depth());
        D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(srv_descriptor_size) * 4;

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = element_count;
        desc.Buffer.StructureByteStride = sizeof(int);
        device->CreateShaderResourceView(sim.gpu_terrain_resource(), &desc, cpu);

        cpu.ptr += srv_descriptor_size;
        desc.Buffer.StructureByteStride = sizeof(float);
        device->CreateShaderResourceView(sim.gpu_water_resource(), &desc, cpu);
    }

    void update_split_lod_buffers(sim::IFieldSim& sim, int w, int d)
    {
        const int detail = split_lod_group_size_cells(sim);
        m_split_coarse_width = static_cast<UINT>((w + detail - 1) / detail);
        m_split_coarse_depth = static_cast<UINT>((d + detail - 1) / detail);
        m_split_fine_count = 0;

        for (UINT coarse_z = 0; coarse_z < m_split_coarse_depth; ++coarse_z)
        {
            for (UINT coarse_x = 0; coarse_x < m_split_coarse_width; ++coarse_x)
            {
                float min_terrain_inches = std::numeric_limits<float>::max();

                const int start_x = static_cast<int>(coarse_x) * detail;
                const int start_z = static_cast<int>(coarse_z) * detail;
                const int end_x = std::min(start_x + detail, w);
                const int end_z = std::min(start_z + detail, d);

                for (int z = start_z; z < end_z; ++z)
                {
                    for (int x = start_x; x < end_x; ++x)
                    {
                        const float surface_inches = sim.surface_height_inches_at(x, z);
                        const float water_inches = sim.water_depth_inches_at(x, z);
                        min_terrain_inches = std::min(min_terrain_inches, surface_inches - water_inches);
                    }
                }

                if (min_terrain_inches == std::numeric_limits<float>::max())
                    min_terrain_inches = 0.0f;

                const float full_base_inches = std::max(
                    0.0f,
                    std::floor(min_terrain_inches / static_cast<float>(k_inches_per_foot)) *
                        static_cast<float>(k_inches_per_foot));
                const UINT coarse_i = coarse_z * m_split_coarse_width + coarse_x;
                m_split_coarse_mapped[coarse_i].full_height_feet =
                    static_cast<float>(full_base_inches) / static_cast<float>(k_inches_per_foot);

                for (int z = start_z; z < end_z; ++z)
                {
                    for (int x = start_x; x < end_x; ++x)
                    {
                        const float surface_inches = sim.surface_height_inches_at(x, z);
                        const float water_inches = sim.water_depth_inches_at(x, z);
                        if (surface_inches <= full_base_inches)
                            continue;

                        if (m_split_fine_count >= m_split_fine_capacity)
                            continue;

                        SplitFineCellGpu& fine = m_split_fine_mapped[m_split_fine_count++];
                        fine.fine_x = static_cast<uint32_t>(x);
                        fine.fine_z = static_cast<uint32_t>(z);
                        fine.base_height_feet =
                            full_base_inches / static_cast<float>(k_inches_per_foot);
                        fine.top_height_feet =
                            surface_inches / static_cast<float>(k_inches_per_foot);
                        fine.water_depth_feet =
                            water_inches / static_cast<float>(k_inches_per_foot);
                        fine.water_side_mask = split_lod_water_side_mask(sim, x, z);
                        fine.velocity_feet_per_second =
                            sim.velocity_magnitude_feet_per_second_at(x, z);
                        fine.sediment_depth_feet =
                            sim.suspended_sediment_inches_at(x, z) /
                            static_cast<float>(k_inches_per_foot);
                        fine.terrain_delta_feet =
                            sim.terrain_delta_inches_at(x, z) /
                            static_cast<float>(k_inches_per_foot);
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
        if (renderer.uses_gpu_resident_fluid_buffers())
        {
            if (!active_sim().has_gpu_resident_field())
                return;
            update_gpu_resident_descriptors();
        }
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

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_fluid_srv_gpu =
            imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();
        gpu_fluid_srv_gpu.ptr += srv_descriptor_size * 4; // slot 4, followed by slot 5

        gfx::RendererFrameArgs args {};
        args.cmd             = command_list.Get();
        args.srv_heap        = imgui_srv_heap.Get();
        args.cb_gpu_va       = m_cb_buffer->GetGPUVirtualAddress();
        args.field_srv_gpu   = field_srv_gpu;
        args.split_lod_srv_gpu = split_lod_srv_gpu;
        args.gpu_fluid_srv_gpu = gpu_fluid_srv_gpu;
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
        m_frame_elapsed_seconds = std::max(0.0, elapsed_ms / 1000.0);

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

    void update_running_simulation()
    {
        if (!m_simulation_running)
            return;

        bool changed = false;
        for (int step = 0; step < k_simulation_steps_per_render_frame; ++step)
        {
            changed = active_sim().step_once() || changed;
        }

        if (changed)
            m_field_buffer_dirty = true;
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
        draw_sim_visual_points(active_sim());

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
            if (m_active_renderer != previous_renderer)
            {
                const int requested_renderer = m_active_renderer;
                m_active_renderer = previous_renderer;
                select_renderer_option(requested_renderer);
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
        ImGui::Text("Map:    %s", seed_map_profile_name(current_sim.seed_map_profile()));

        {
            const char* names[16] = {};
            const int count = static_cast<int>(
                std::min(m_sims.size(), std::size_t{ 16 }));
            for (int i = 0; i < count; ++i)
                names[i] = m_sims[static_cast<std::size_t>(i)]->name();

            const int previous_active_sim = m_active_sim;
            ImGui::Combo("##simulator", &m_active_sim, names, count);
            if (m_active_sim != previous_active_sim)
            {
                const int requested_sim = m_active_sim;
                m_active_sim = previous_active_sim;
                select_simulator_option(requested_sim);
            }
        }

        // Reset Active re-seeds only the visible experiment. Reset All is useful
        // when comparing experiments from the same starting terrain.
        if (ImGui::Button("Reset Active"))
            reset_field();

        ImGui::SameLine();

        if (ImGui::Button("Reset All"))
            reset_all_simulators();

        if (ImGui::Button(m_simulation_running ? "Pause" : "Run"))
            m_simulation_running = !m_simulation_running;

        ImGui::SameLine();
        ImGui::TextDisabled(m_simulation_running
            ? "Running 4 steps/rendered frame"
            : "Paused (4 steps/rendered frame)");

        ImGui::Separator();

        sim::IFieldSim& ui_sim = active_sim();
        const bool selected_cell_valid = selected_cell_valid_for(ui_sim);
        ui_sim.set_selected_cell(m_selected_x, m_selected_z, selected_cell_valid);

        // Delegate all simulator-specific UI to the simulator itself.
        if (ui_sim.render_ui())
            m_field_buffer_dirty = true;

        ImGui::End();

        // ── Lesson guide panel ───────────────────────────────────────────────
        // This provides a compact in-app map to the full Markdown lessons and
        // switches between the already-implemented experiments for comparison.
        ImGui::SetNextWindowPos(ImVec2(450, 32), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
        ImGui::Begin("Lesson Guide");

        const char* lesson_names[k_experiment_lessons.size()] = {};
        for (std::size_t i = 0; i < k_experiment_lessons.size(); ++i)
            lesson_names[i] = k_experiment_lessons[i].display_name;

        ImGui::TextDisabled("CPU lessons first, then other lessons by name.");
        ImGui::Combo(
            "Lesson", &m_active_lesson, lesson_names,
            static_cast<int>(k_experiment_lessons.size()));
        m_active_lesson = std::clamp(
            m_active_lesson, 0, static_cast<int>(k_experiment_lessons.size()) - 1);

        const ExperimentLessonUnit& lesson =
            k_experiment_lessons[static_cast<std::size_t>(m_active_lesson)];
        ImGui::Separator();
        ImGui::Text("%s", lesson.group);
        ImGui::TextWrapped("%s", lesson.lesson_file);

        if (ImGui::RadioButton("Overview", m_lesson_view == 0))
            m_lesson_view = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Compare Options", m_lesson_view == 1))
            m_lesson_view = 1;

        ImGui::Separator();
        ImGui::TextWrapped("%s", m_lesson_view == 0 ? lesson.overview : lesson.comparison);

        const bool primary_exists = lesson.option_kind == LessonOptionKind::Simulation
            ? lesson.primary_option < static_cast<int>(m_sims.size())
            : lesson.primary_option < static_cast<int>(m_renderers.size());
        if (primary_exists && ImGui::Button(lesson.primary_label))
        {
            if (lesson.option_kind == LessonOptionKind::Simulation)
                select_simulator_option(lesson.primary_option);
            else
                select_renderer_option(lesson.primary_option);
        }

        ImGui::SameLine();
        const bool alternate_exists = lesson.option_kind == LessonOptionKind::Simulation
            ? lesson.alternate_option < static_cast<int>(m_sims.size())
            : lesson.alternate_option < static_cast<int>(m_renderers.size());
        if (alternate_exists && ImGui::Button(lesson.alternate_label))
        {
            if (lesson.option_kind == LessonOptionKind::Simulation)
                select_simulator_option(lesson.alternate_option);
            else
                select_renderer_option(lesson.alternate_option);
        }

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
        if (selected_cell_valid_for(active_sim()))
            ImGui::Text("Orbit:    [%d, %d]", m_selected_x, m_selected_z);
        else
            ImGui::TextDisabled("Orbit: field center");
        ImGui::Separator();
        ImGui::TextDisabled("Left-click a cell to set orbit focus");
        ImGui::TextDisabled("Middle-drag to orbit / Right-drag to pan / Scroll to zoom");
        if (ImGui::Button("Reset Camera"))
        {
            m_camera = gfx::OrbitCamera{ -90.f, 45.f, 130.f };
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
        sim::IFieldSim& column_sim = active_sim();
        const bool selected_column_valid = selected_cell_valid_for(column_sim);
        const bool hovered_column_valid =
            m_hovered_column_valid &&
            cell_in_bounds(column_sim, m_hovered_x, m_hovered_z);
        const bool has_column = selected_column_valid || hovered_column_valid;
        const int column_x = selected_column_valid ? m_selected_x : m_hovered_x;
        const int column_z = selected_column_valid ? m_selected_z : m_hovered_z;

        if (has_column)
        {
            const float h_inches = column_sim.surface_height_inches_at(column_x, column_z);
            const float water_inches = column_sim.water_depth_inches_at(column_x, column_z);
            ImGui::Text("%s [%d, %d]",
                selected_column_valid ? "Selected" : "Hovered",
                column_x, column_z);
            ImGui::Text("Surface: %.2f inches", h_inches);
            ImGui::Text("        %.2f feet", h_inches / 12.f);
            if (water_inches > 0.001f)
                ImGui::Text("Water:  %.2f inches", water_inches);
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
            update_running_simulation();
            handle_pending_resize();
            if (width == 0 || height == 0)
                continue;
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
            if (m_field_buffer_dirty &&
                !(active_sim().has_gpu_resident_field() &&
                  active_renderer().uses_gpu_resident_fluid_buffers()))
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
            if (active_sim().has_gpu_resident_field())
                wait_for_gpu();
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
        app.initialize_gpu_simulators();
        app.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Grass Field 003 — Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
