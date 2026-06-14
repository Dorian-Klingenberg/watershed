#pragma once

// raycast_renderer.h — Column Raycast renderer (extracted from Step 5)
//
// Renders the terrain using a single full-screen triangle and a DDA (Digital
// Differential Analysis) column-raycast pixel shader. No vertex buffer or mesh
// is needed — the vertex shader generates a triangle covering the entire NDC
// square from just three hard-coded vertex IDs (0, 1, 2).
//
// Technique summary:
//   For each pixel the PS fires a ray through the column grid. It uses DDA to
//   hop from column boundary to column boundary (always along the nearer axis),
//   checking each column's height until it either hits a surface or escapes.
//   On a hit it computes a diffuse shading normal and outputs a terrain colour.
//
// Root signature:
//   Param 0 -> inline root CBV at b0  (SceneConstants, VISIBILITY_ALL)
//   Param 1 -> descriptor table:
//              - normal mode: one SRV at t0 for CPU-composed column heights
//              - GPU-resident mode: terrain at t0 and water at t1
//             The VS ignores the field SRV(s); only the PS samples them.
//
// Shaders (compiled by CMake via fxc):
//   grass_field_vs.cso / grass_field_ps.cso
//   gpu_fluid_raycast_vs.cso / gpu_fluid_raycast_ps.cso

#include "i_field_renderer.h"
#include "shader_utils.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <imgui.h>
#include <stdexcept>
#include <string>

namespace grannys_house_trials::gfx {

class RaycastRenderer final : public IFieldRenderer
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    bool m_gpu_resident_fluid = false;

public:
    explicit RaycastRenderer(bool gpu_resident_fluid = false)
        : m_gpu_resident_fluid(gpu_resident_fluid)
    {
    }

    const char* name() const noexcept override
    {
        return m_gpu_resident_fluid
            ? "GPU Resident Fluid Raycast"
            : "Column Raycast (DDA)";
    }

    // ── IFieldRenderer::initialize ────────────────────────────────────────────

    void initialize(const RendererInitContext& ctx) override
    {
        build_root_signature(ctx.device);
        build_pso(ctx.device, ctx.rtv_format);
    }

    // ── IFieldRenderer::record_draw ──────────────────────────────────────────

    void record_draw(const RendererFrameArgs& args) override
    {
        // Bind our root signature — tells the GPU how to interpret root params.
        args.cmd->SetGraphicsRootSignature(m_root_signature.Get());

        // Bind the shared shader-visible heap that holds both ImGui's font
        // atlas (slot 0) and our field heights SRV (slot 1). Must be set
        // before any descriptor-table root parameter.
        args.cmd->SetDescriptorHeaps(1, &args.srv_heap);

        // Root param 0: inline CBV — points directly to the SceneConstants
        // upload buffer by GPU VA. No descriptor heap slot needed.
        args.cmd->SetGraphicsRootConstantBufferView(0, args.cb_gpu_va);

        // Root param 1: descriptor table. Normal mode starts at heap slot 1
        // (composed field cells); resident mode starts at slot 4 (terrain,
        // followed by water).
        args.cmd->SetGraphicsRootDescriptorTable(
            1, m_gpu_resident_fluid ? args.gpu_fluid_srv_gpu : args.field_srv_gpu);

        args.cmd->SetPipelineState(m_pso.Get());
        args.cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Viewport and scissor are required state even for a full-screen pass.
        // If not set the rasterizer clips everything to an empty rect.
        const D3D12_VIEWPORT vp = {
            0.f, 0.f,
            static_cast<float>(args.viewport_width),
            static_cast<float>(args.viewport_height),
            0.f, 1.f
        };
        const D3D12_RECT scissor = {
            0, 0,
            static_cast<LONG>(args.viewport_width),
            static_cast<LONG>(args.viewport_height)
        };
        args.cmd->RSSetViewports(1, &vp);
        args.cmd->RSSetScissorRects(1, &scissor);

        // 3 vertices = one full-screen triangle. No vertex buffer.
        // The VS derives clip-space positions purely from SV_VertexID.
        args.cmd->DrawInstanced(3, 1, 0, 0);
    }

    // ── IFieldRenderer::render_ui ─────────────────────────────────────────────

    void render_ui() override
    {
        // No per-renderer settings exposed yet. The DDA step count and
        // shading parameters are currently hard-coded in the HLSL.
        ImGui::TextDisabled(m_gpu_resident_fluid
            ? "Reads terrain and water directly from compute buffers."
            : "DDA column raycast - no settings.");
    }

    [[nodiscard]] bool uses_gpu_resident_fluid_buffers() const noexcept override
    {
        return m_gpu_resident_fluid;
    }

private:
    // ── Root signature ────────────────────────────────────────────────────────
    // Two parameters:
    //   0 - inline root CBV at b0: SceneConstants (no descriptor heap slot)
    //   1 - descriptor table: one or two SRVs, pixel-visible
    void build_root_signature(ID3D12Device* device)
    {
        D3D12_ROOT_PARAMETER params[2] = {};

        // Param 0: inline CBV — cheaper than a descriptor table; the VA is
        // embedded directly in the root signature, saving a heap indirection.
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;  // b0 in HLSL
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        // Param 1: descriptor table. Normal raycast needs one SRV; GPU-resident
        // raycast needs two adjacent SRVs for terrain and water.
        // PIXEL only: the VS for this renderer does NOT sample the height field;
        // it just hard-codes three clip-space corners from SV_VertexID.
        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = m_gpu_resident_fluid ? 2 : 1;
        srv_range.BaseShaderRegister                = 0;  // t0 in HLSL
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 2;
        desc.pParameters   = params;
        // Deny unused pipeline stages — a driver hint that can improve perf.
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS   |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        Microsoft::WRL::ComPtr<ID3DBlob> sig_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> err_blob;
        const HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &err_blob);
        if (FAILED(hr))
            throw std::runtime_error(err_blob
                ? std::string(static_cast<const char*>(err_blob->GetBufferPointer()))
                : "RaycastRenderer: D3D12SerializeRootSignature failed.");

        if (FAILED(device->CreateRootSignature(
                0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                IID_PPV_ARGS(&m_root_signature))))
            throw std::runtime_error("RaycastRenderer: CreateRootSignature failed.");
    }

    // ── PSO ───────────────────────────────────────────────────────────────────
    // Solid fill, no culling (the full-screen triangle has no back face),
    // no depth buffer (we manage depth manually in the PS via DDA).
    void build_pso(ID3D12Device* device, DXGI_FORMAT rtv_format)
    {
        // Load pre-compiled shader blobs from the directory next to the exe.
        const auto dir = exe_dir();
        const auto vs = load_shader_blob(to_utf8(dir + (m_gpu_resident_fluid
            ? L"shaders/gpu_fluid_raycast_vs.cso"
            : L"shaders/grass_field_vs.cso")));
        const auto ps = load_shader_blob(to_utf8(dir + (m_gpu_resident_fluid
            ? L"shaders/gpu_fluid_raycast_ps.cso"
            : L"shaders/grass_field_ps.cso")));

        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable           = FALSE;
        blend.RenderTarget[0].LogicOpEnable         = FALSE;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_RASTERIZER_DESC raster = {};
        raster.FillMode              = D3D12_FILL_MODE_SOLID;
        raster.CullMode              = D3D12_CULL_MODE_NONE;   // full-screen tri, no back face
        raster.FrontCounterClockwise = FALSE;
        raster.DepthClipEnable       = TRUE;
        raster.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable   = FALSE;  // no depth buffer — PS manages occlusion via DDA
        ds.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature        = m_root_signature.Get();
        pso_desc.VS                    = { vs.data(), vs.size() };
        pso_desc.PS                    = { ps.data(), ps.size() };
        pso_desc.BlendState            = blend;
        pso_desc.RasterizerState       = raster;
        pso_desc.DepthStencilState     = ds;
        pso_desc.SampleMask            = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets      = 1;
        pso_desc.RTVFormats[0]         = rtv_format;
        pso_desc.SampleDesc.Count      = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&m_pso))))
            throw std::runtime_error("RaycastRenderer: CreateGraphicsPipelineState failed.");
    }
};

} // namespace grannys_house_trials::gfx
