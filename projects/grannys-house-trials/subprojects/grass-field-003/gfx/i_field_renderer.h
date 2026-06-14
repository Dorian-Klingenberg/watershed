#pragma once

// i_field_renderer.h — pluggable renderer interface for grass-field-003
//
// Any class that can draw the terrain into a D3D12 command list implements
// this interface. Application holds a list of these and lets the user pick
// one at runtime via an ImGui combo. Switching renderers requires no
// changes to Application — only the active renderer index changes.
//
// THREE CONTRACTS:
//
//   LIFECYCLE CONTRACT
//       initialize() — called once at startup (or when a renderer is first
//                      activated). Builds the root signature and PSO using
//                      the provided device and format context.
//
//   RENDER CONTRACT
//       record_draw() — called every frame BEFORE ImGui recording.
//                       The command list is already open and the back buffer
//                       is already in RENDER_TARGET state and OMSetRenderTargets
//                       has been called. The renderer appends its draw calls.
//
//   UI CONTRACT
//       name()        — short display string for the renderer picker combo.
//       render_ui()   — draws renderer-specific ImGui controls inside an
//                       already-open window. Must NOT call Begin() / End().
//                       Default implementation is a no-op.

#include <d3d12.h>
#include <dxgi1_6.h>

namespace grannys_house_trials::gfx {

// All GPU context data passed to initialize().
// Each renderer uses this to create its own root signature and PSO.
struct RendererInitContext
{
    ID3D12Device* device;           // The D3D12 device — for CreateRootSignature etc.
    DXGI_FORMAT   rtv_format;       // Back-buffer format — must match the PSO's RTVFormats[0].
    DXGI_FORMAT   dsv_format;       // Depth-buffer format for renderers that use depth testing.
    UINT          srv_descriptor_size; // Stride of the shared CBV/SRV/UAV heap.
};

// All per-frame data passed to record_draw().
// Everything a renderer needs to emit its draw calls — no raw Application ptr.
struct RendererFrameArgs
{
    ID3D12GraphicsCommandList*  cmd;            // Open command list to record into.
    ID3D12DescriptorHeap*       srv_heap;       // Shared CBV/SRV/UAV heap (ImGui + field SRV).
    D3D12_GPU_VIRTUAL_ADDRESS   cb_gpu_va;      // GPU VA of the SceneConstants upload buffer.
    D3D12_GPU_DESCRIPTOR_HANDLE field_srv_gpu;  // GPU handle of heap slot 1 (column heights SRV).
    D3D12_GPU_DESCRIPTOR_HANDLE split_lod_srv_gpu; // Heap slot 2: split coarse SRV, followed by fine SRV.
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_fluid_srv_gpu; // Heap slot 4: resident terrain SRV, followed by water SRV.
    UINT                        viewport_width; // Back buffer / viewport width in pixels.
    UINT                        viewport_height;// Back buffer / viewport height in pixels.
    UINT                        field_width;    // Column count along X — used by wireframe draw.
    UINT                        field_depth;    // Column count along Z — used by wireframe draw.
    UINT                        split_coarse_width;  // One-foot coarse cells along X.
    UINT                        split_coarse_depth;  // One-foot coarse cells along Z.
    UINT                        split_fine_count;    // Compact fine-remainder column count.
};

// Abstract base class for all terrain renderers.
// Each concrete renderer owns its own root signature + PSO so it is free to
// choose different shaders, fill modes, and descriptor binding layouts.
class IFieldRenderer
{
public:
    virtual ~IFieldRenderer() = default;

    // Human-readable name shown in the renderer picker combo.
    virtual const char* name() const noexcept = 0;

    // One-time GPU resource creation. Must be called before record_draw().
    // Renderers are all initialized at startup so switching is instant.
    virtual void initialize(const RendererInitContext& ctx) = 0;

    // Record terrain draw commands into the open command list.
    // Called every frame; do not Close() the list or transition the back buffer.
    virtual void record_draw(const RendererFrameArgs& args) = 0;

    // Optional renderer requirements. Most renderers use only the colour target
    // and the main field buffer; specialized renderers opt into extra resources.
    [[nodiscard]] virtual bool uses_depth_buffer() const noexcept { return false; }
    [[nodiscard]] virtual bool uses_split_lod_buffers() const noexcept { return false; }
    [[nodiscard]] virtual bool uses_gpu_resident_fluid_buffers() const noexcept { return false; }

    // Draw this renderer's own ImGui controls.
    // Called inside an already-open ImGui window — must NOT call Begin()/End().
    virtual void render_ui() {}
};

} // namespace grannys_house_trials::gfx
