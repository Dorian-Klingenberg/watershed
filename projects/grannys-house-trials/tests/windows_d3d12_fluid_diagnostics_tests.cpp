#include <catch2/catch_test_macros.hpp>

#include "native_d3d12_diagnostics.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <limits>

using Microsoft::WRL::ComPtr;

namespace
{
int d3d12_removed_reason(ID3D12Device* device)
{
    return static_cast<int>(device ? device->GetDeviceRemovedReason() : S_OK);
}
}

TEST_CASE("D3D12 initialization report records ordered failures", "[windows_d3d12]")
{
    windows_d3d12_fluid::InitializationReport report;
    report.record(
        windows_d3d12_fluid::InitializationStage::d3d12_device,
        S_OK,
        S_OK,
        12);
    report.record(
        windows_d3d12_fluid::InitializationStage::root_signature_create,
        DXGI_ERROR_DEVICE_REMOVED,
        DXGI_ERROR_INVALID_CALL,
        30);

    REQUIRE(report.observations().size() == 2);
    REQUIRE(report.observations().front().elapsed_ms == 12);
    REQUIRE(report.observations().back().hresult == DXGI_ERROR_DEVICE_REMOVED);
    REQUIRE(report.observations().back().device_removed_reason == DXGI_ERROR_INVALID_CALL);
    REQUIRE(report.has_failure());

    const std::string text = report.text();
    REQUIRE(text.find("root_signature_create") != std::string::npos);
    REQUIRE(text.find("887A0005") != std::string::npos);
    REQUIRE(text.find("887A0001") != std::string::npos);
}

TEST_CASE("D3D12 same-state resource transitions are omitted", "[windows_d3d12]")
{
    REQUIRE(windows_d3d12_fluid::is_redundant_resource_transition(0x4u, 0x4u));
    REQUIRE_FALSE(windows_d3d12_fluid::is_redundant_resource_transition(0x4u, 0x8u));
}

TEST_CASE("D3D12 tick batches match the documented 1, 2, 4, 8 sequence", "[windows_d3d12]")
{
    REQUIRE(windows_d3d12_fluid::is_valid_tick_batch(1));
    REQUIRE(windows_d3d12_fluid::is_valid_tick_batch(2));
    REQUIRE(windows_d3d12_fluid::is_valid_tick_batch(4));
    REQUIRE(windows_d3d12_fluid::is_valid_tick_batch(8));
    REQUIRE_FALSE(windows_d3d12_fluid::is_valid_tick_batch(3));
    REQUIRE_FALSE(windows_d3d12_fluid::is_valid_tick_batch(16));
}

TEST_CASE("D3D12 normalized picking maps into the 192 by 128 field", "[windows_d3d12]")
{
    REQUIRE(windows_d3d12_fluid::normalized_field_coordinate(0.5f, 0.5f, 1.0f, 192) == 96);
    REQUIRE(windows_d3d12_fluid::normalized_field_coordinate(0.5f, 0.5f, 1.0f, 128) == 64);
    REQUIRE(windows_d3d12_fluid::normalized_field_coordinate(0.0f, 0.5f, 1.0f, 192) == 0);
    REQUIRE(windows_d3d12_fluid::normalized_field_coordinate(1.0f, 0.5f, 1.0f, 128) == 127);
    REQUIRE(windows_d3d12_fluid::normalized_field_coordinate(1.5f, 0.5f, 1.0f, 192) == 191);
}

TEST_CASE("D3D12 orbit view normalizes yaw and clamps elevation", "[windows_d3d12]")
{
    REQUIRE(windows_d3d12_fluid::normalized_view_yaw(450.0f) == 90.0f);
    REQUIRE(windows_d3d12_fluid::normalized_view_yaw(-45.0f) == 315.0f);
    REQUIRE(windows_d3d12_fluid::normalized_view_yaw(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    REQUIRE(windows_d3d12_fluid::clamped_view_elevation(5.0f) == 15.0f);
    REQUIRE(windows_d3d12_fluid::clamped_view_elevation(45.0f) == 45.0f);
    REQUIRE(windows_d3d12_fluid::clamped_view_elevation(95.0f) == 80.0f);
}

TEST_CASE("D3D12 exact app field texture and SRV create at feature level 11", "[windows_d3d12]")
{
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device));
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(device);

    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = 192;
    textureDescription.Height = 128;
    textureDescription.DepthOrArraySize = 1;
    textureDescription.MipLevels = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));
    REQUIRE(SUCCEEDED(hr));

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.NumDescriptors = 1;
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    hr = device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&srvHeap));
    REQUIRE(SUCCEEDED(hr));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
    srvDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescription.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        texture.Get(),
        &srvDescription,
        srvHeap->GetCPUDescriptorHandleForHeapStart());

    REQUIRE(d3d12_removed_reason(device.Get()) == S_OK);
}

TEST_CASE("D3D12 exact app root signature creates at feature level 11", "[windows_d3d12]")
{
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device));
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(device);

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 1;
    description.pParameters = &parameter;
    description.NumStaticSamplers = 0;
    description.pStaticSamplers = nullptr;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    hr = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &errors);
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(signature);

    ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    REQUIRE(SUCCEEDED(hr));
    REQUIRE(rootSignature);
    REQUIRE(d3d12_removed_reason(device.Get()) == S_OK);
}
