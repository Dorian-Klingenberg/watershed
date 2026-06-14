#pragma once

// wireframe_renderer.h — Top-Face Wireframe renderer
//
// Renders the terrain as a wireframe mesh showing the top face of every column.
// Each column at (cx, cz) contributes one quad (two triangles = 6 vertices).
// No vertex buffer is needed — the VS generates all world-space positions from
// SV_VertexID plus the column_heights_feet StructuredBuffer.
//
// With D3D12_FILL_MODE_WIREFRAME the rasterizer draws only triangle edges,
// producing a grid-line view that makes height changes and erosion patterns
// clearly visible without any raycast cost.
//
// Root signature:
//   Param 0 → inline root CBV at b0  (SceneConstants, VISIBILITY_ALL)
//   Param 1 → descriptor table: 1 SRV at t0  (column heights, VISIBILITY_VERTEX)
//             Unlike the raycast renderer, heights are read in the VS here
//             (to position vertices), not in the PS.
//
// Key difference from RaycastRenderer:
//   • SRV is VERTEX-visible so the VS can index column_heights_feet.
//   • Uses view_projection from SceneConstants (the forward VP matrix)
//     rather than inverse_view_projection, since we have world positions.
//   • FillMode = WIREFRAME — only edges are drawn.
//   • DrawInstanced vertex count = field_width * field_depth * 6.
//
// Shaders (compiled by CMake via fxc):
//   wireframe_vs.cso  (compiled from wireframe_renderer.hlsl, VSMain)
//   wireframe_ps.cso  (compiled from wireframe_renderer.hlsl, PSMain)

#include "i_field_renderer.h"
#include "shader_utils.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <imgui.h>
#include <stdexcept>
#include <string>

namespace grannys_house_trials::gfx {

class WireframeRenderer final : public IFieldRenderer
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

public:
    const char* name() const noexcept override { return "Wireframe (Top Faces)"; }

    // ── IFieldRenderer::initialize ────────────────────────────────────────────

    void initialize(const RendererInitContext& ctx) override
    {
        build_root_signature(ctx.device);
        build_pso(ctx.device, ctx.rtv_format);
    }

    // ── IFieldRenderer::record_draw ──────────────────────────────────────────

    void record_draw(const RendererFrameArgs& args) override
    {
        args.cmd->SetGraphicsRootSignature(m_root_signature.Get());
        args.cmd->SetDescriptorHeaps(1, &args.srv_heap);

        // Root param 0: inline CBV (SceneConstants, includes view_projection).
        args.cmd->SetGraphicsRootConstantBufferView(0, args.cb_gpu_va);

        // Root param 1: descriptor table — GPU handle of heap slot 1 (heights).
        // The VS reads this to position each quad corner at the column's height.
        args.cmd->SetGraphicsRootDescriptorTable(1, args.field_srv_gpu);

        args.cmd->SetPipelineState(m_pso.Get());
        args.cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

        // 6 vertices per column (2 triangles = one top-face quad), no index buffer.
        // The VS derives the quad corner from (SV_VertexID % 6) and the column
        // position from (SV_VertexID / 6).
        const UINT vertex_count = args.field_width * args.field_depth * 6u;
        args.cmd->DrawInstanced(vertex_count, 1, 0, 0);
    }

    // ── IFieldRenderer::render_ui ─────────────────────────────────────────────

    void render_ui() override
    {
        ImGui::TextDisabled("Top-face grid — no settings.");
    }

private:
    // ── Root signature ────────────────────────────────────────────────────────
    // Same two-parameter layout as RaycastRenderer, but param 1 (the heights SRV)
    // is VERTEX-visible instead of PIXEL-visible, because the wireframe VS
    // reads column heights to place vertices.
    void build_root_signature(ID3D12Device* device)
    {
        D3D12_ROOT_PARAMETER params[2] = {};

        // Param 0: inline CBV — SceneConstants (includes view_projection).
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;  // b0 in HLSL
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        // Param 1: descriptor table — one SRV at t0 (column heights).
        // VERTEX-visible: the wireframe VS reads heights to position quad corners.
        // (The raycast renderer uses PIXEL-visible because its VS has no mesh.)
        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 1;
        srv_range.BaseShaderRegister                = 0;  // t0 in HLSL
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 2;
        desc.pParameters   = params;
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
                : "WireframeRenderer: D3D12SerializeRootSignature failed.");

        if (FAILED(device->CreateRootSignature(
                0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                IID_PPV_ARGS(&m_root_signature))))
            throw std::runtime_error("WireframeRenderer: CreateRootSignature failed.");
    }

    // ── PSO ───────────────────────────────────────────────────────────────────
    // Wireframe fill — the rasterizer draws only triangle edges.
    // CullMode_None is required here: with FILL_MODE_WIREFRAME and back-face
    // culling enabled, the GPU could silently drop "back" edges of top-facing
    // quads depending on winding order and view direction.
    void build_pso(ID3D12Device* device, DXGI_FORMAT rtv_format)
    {
        const auto dir = exe_dir();
        const auto vs  = load_shader_blob(to_utf8(dir + L"shaders/wireframe_vs.cso"));
        const auto ps  = load_shader_blob(to_utf8(dir + L"shaders/wireframe_ps.cso"));

        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable           = FALSE;
        blend.RenderTarget[0].LogicOpEnable         = FALSE;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_RASTERIZER_DESC raster = {};
        raster.FillMode              = D3D12_FILL_MODE_WIREFRAME; // edges only, no fill
        raster.CullMode              = D3D12_CULL_MODE_NONE;
        raster.FrontCounterClockwise = FALSE;
        raster.DepthClipEnable       = TRUE;
        raster.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable   = FALSE;
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
            throw std::runtime_error("WireframeRenderer: CreateGraphicsPipelineState failed.");
    }
};

} // namespace grannys_house_trials::gfx
