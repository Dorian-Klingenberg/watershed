#pragma once

// split_lod_renderer.h -- Coarse base + fine remainder renderer.
//
// This renderer keeps the simulation grid unchanged, but draws it through two
// mesh passes:
//   1. Coarse pass: one 12-inch render column per original 1-foot patch, up to
//      the height that is completely full across that whole 12x12 fine patch.
//   2. Fine pass: compacted 1-inch remainder columns above that coarse base.
//
// The two passes use a depth buffer so the fine remainder naturally overlays the
// cheaper coarse base wherever both cover the same screen pixels.

#include "i_field_renderer.h"
#include "shader_utils.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <imgui.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace grannys_house_trials::gfx {

class SplitLodRenderer final : public IFieldRenderer
{
    struct PassConstants
    {
        std::uint32_t pass_kind = 0; // 0 = coarse, 1 = fine remainder
        std::uint32_t coarse_width = 0;
        std::uint32_t coarse_depth = 0;
        std::uint32_t pad0 = 0;
    };

    static constexpr UINT k_vertices_per_column = 36;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

public:
    const char* name() const noexcept override { return "Split Coarse/Fine Columns"; }

    void initialize(const RendererInitContext& ctx) override
    {
        build_root_signature(ctx.device);
        build_pso(ctx.device, ctx.rtv_format, ctx.dsv_format);
    }

    void record_draw(const RendererFrameArgs& args) override
    {
        if (args.split_coarse_width == 0 || args.split_coarse_depth == 0)
            return;

        args.cmd->SetGraphicsRootSignature(m_root_signature.Get());
        args.cmd->SetDescriptorHeaps(1, &args.srv_heap);
        args.cmd->SetGraphicsRootConstantBufferView(0, args.cb_gpu_va);
        args.cmd->SetGraphicsRootDescriptorTable(1, args.split_lod_srv_gpu);

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

        PassConstants pass {};
        pass.coarse_width = args.split_coarse_width;
        pass.coarse_depth = args.split_coarse_depth;

        pass.pass_kind = 0;
        args.cmd->SetGraphicsRoot32BitConstants(2, 4, &pass, 0);
        args.cmd->DrawInstanced(
            args.split_coarse_width * args.split_coarse_depth * k_vertices_per_column,
            1, 0, 0);

        if (args.split_fine_count > 0)
        {
            pass.pass_kind = 1;
            args.cmd->SetGraphicsRoot32BitConstants(2, 4, &pass, 0);
            args.cmd->DrawInstanced(
                args.split_fine_count * k_vertices_per_column,
                1, 0, 0);
        }
    }

    void render_ui() override
    {
        ImGui::TextDisabled("Depth-tested coarse full columns + compact fine remainder.");
    }

    [[nodiscard]] bool uses_depth_buffer() const noexcept override { return true; }
    [[nodiscard]] bool uses_split_lod_buffers() const noexcept override { return true; }

private:
    void build_root_signature(ID3D12Device* device)
    {
        D3D12_ROOT_PARAMETER params[3] = {};

        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0; // b0 SceneConstants
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srv_range = {};
        srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors                    = 2; // t0 coarse, t1 fine
        srv_range.BaseShaderRegister                = 0;
        srv_range.RegisterSpace                     = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srv_range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;

        params[2].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants.ShaderRegister = 1; // b1 SplitPassConstants
        params[2].Constants.RegisterSpace  = 0;
        params[2].Constants.Num32BitValues = 4;
        params[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 3;
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
                : "SplitLodRenderer: D3D12SerializeRootSignature failed.");

        if (FAILED(device->CreateRootSignature(
                0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
                IID_PPV_ARGS(&m_root_signature))))
            throw std::runtime_error("SplitLodRenderer: CreateRootSignature failed.");
    }

    void build_pso(ID3D12Device* device, DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format)
    {
        const auto dir = exe_dir();
        const auto vs  = load_shader_blob(to_utf8(dir + L"shaders/split_lod_vs.cso"));
        const auto ps  = load_shader_blob(to_utf8(dir + L"shaders/split_lod_ps.cso"));

        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable           = FALSE;
        blend.RenderTarget[0].LogicOpEnable         = FALSE;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_RASTERIZER_DESC raster = {};
        raster.FillMode              = D3D12_FILL_MODE_SOLID;
        raster.CullMode              = D3D12_CULL_MODE_NONE;
        raster.FrontCounterClockwise = FALSE;
        raster.DepthClipEnable       = TRUE;
        raster.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable    = TRUE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        ds.StencilEnable  = FALSE;

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
        pso_desc.DSVFormat             = dsv_format;
        pso_desc.SampleDesc.Count      = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&m_pso))))
            throw std::runtime_error("SplitLodRenderer: CreateGraphicsPipelineState failed.");
    }
};

} // namespace grannys_house_trials::gfx
