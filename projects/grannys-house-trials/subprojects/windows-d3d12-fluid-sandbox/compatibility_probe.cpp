#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
const char* feature_level_name(D3D_FEATURE_LEVEL level)
{
    switch (level)
    {
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    default: return "unknown";
    }
}

const char* dxgi_adapter_name(const DXGI_ADAPTER_DESC1& description)
{
    char* output = nullptr;
    static char asciiDescription[128]{};
    std::memset(asciiDescription, 0, sizeof(asciiDescription));
    for (std::size_t i = 0; i < 127 && description.Description[i] != L'\0'; ++i)
        asciiDescription[i] = static_cast<char>(description.Description[i] & 0xff);
    return asciiDescription;
}

void print_hresult(const char* operation, HRESULT hr)
{
    std::printf("  %-34s hr=0x%08X\n", operation, static_cast<unsigned int>(hr));
}

std::vector<std::byte> read_file(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

void probe_exact_app_pipeline(ID3D12Device* device, D3D_FEATURE_LEVEL level)
{
    if (level != D3D_FEATURE_LEVEL_11_0)
        return;

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
    HRESULT hr = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &errors);
    print_hresult("Exact app: serialize root sig", hr);
    if (FAILED(hr))
        return;

    ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    print_hresult("Exact app: create root sig", hr);
    if (FAILED(hr))
        return;

    const char* vertexPath =
        "projects/grannys-house-trials/subprojects/windows-d3d12-fluid-sandbox/native/shaders/field_render_vs.cso";
    const char* pixelPath =
        "projects/grannys-house-trials/subprojects/windows-d3d12-fluid-sandbox/native/shaders/field_render_ps.cso";
    const std::vector<std::byte> vertex = read_file(vertexPath);
    const std::vector<std::byte> pixel = read_file(pixelPath);
    std::printf("  Exact app: VS bytes=%zu PS bytes=%zu\n", vertex.size(), pixel.size());
    if (vertex.empty() || pixel.empty())
        return;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = rootSignature.Get();
    pipeline.VS = { vertex.data(), vertex.size() };
    pipeline.PS = { pixel.data(), pixel.size() };
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pipeline.RasterizerState.FrontCounterClockwise = TRUE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.SampleMask = UINT_MAX;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipelineState;
    hr = device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&pipelineState));
    print_hresult("Exact app: create graphics PSO", hr);
}

void probe_exact_composition_swap_chain(ID3D12Device* device)
{
    std::printf("\n[EXACT COMPOSITION SWAP-CHAIN]\n");
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    print_hresult("CreateDXGIFactory2", hr);
    if (FAILED(hr))
        return;

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    hr = device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue));
    print_hresult("CreateCommandQueue", hr);
    if (FAILED(hr))
        return;

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = 960;
    description.Height = 640;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForComposition(queue.Get(), &description, nullptr, &swapChain);
    print_hresult("CreateSwapChainForComposition", hr);
    if (FAILED(hr))
        return;

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.NumDescriptors = 2;
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    hr = device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&rtvHeap));
    print_hresult("CreateDescriptorHeap(RTV)", hr);
    if (FAILED(hr))
        return;

    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < 2; ++index)
    {
        ComPtr<ID3D12Resource> renderTarget;
        hr = swapChain->GetBuffer(index, IID_PPV_ARGS(&renderTarget));
        print_hresult(index == 0 ? "SwapChain GetBuffer 0" : "SwapChain GetBuffer 1", hr);
        if (FAILED(hr))
            break;
        device->CreateRenderTargetView(renderTarget.Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;
    }

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
    ComPtr<ID3D12Resource> fieldTexture;
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&fieldTexture));
    print_hresult("CreateCommittedResource(texture)", hr);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const UINT rowPitch = (192 * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    D3D12_RESOURCE_DESC uploadDescription{};
    uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDescription.Width = static_cast<UINT64>(rowPitch) * 128;
    uploadDescription.Height = 1;
    uploadDescription.DepthOrArraySize = 1;
    uploadDescription.MipLevels = 1;
    uploadDescription.Format = DXGI_FORMAT_UNKNOWN;
    uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDescription.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> fieldUpload;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDescription,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&fieldUpload));
    print_hresult("CreateCommittedResource(upload)", hr);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
    srvHeapDescription.NumDescriptors = 1;
    srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    hr = device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&srvHeap));
    print_hresult("CreateDescriptorHeap(SRV)", hr);

    if (SUCCEEDED(hr) && fieldTexture)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDescription.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            fieldTexture.Get(),
            &srvDescription,
            srvHeap->GetCPUDescriptorHandleForHeapStart());
        print_hresult("CreateShaderResourceView(texture)", S_OK);
    }

    HRESULT removedReason = device->GetDeviceRemovedReason();
    print_hresult("GetDeviceRemovedReason", removedReason);
}

void probe_d3d12_adapters()
{
    std::printf("\n[D3D12 ADAPTER ENUMERATION]\n");
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    print_hresult("CreateDXGIFactory2", hr);
    if (FAILED(hr))
        return;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
    {
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)))
        {
            std::printf("  Adapter %u: %s flags=0x%X dedicatedMB=%llu\n",
                index,
                dxgi_adapter_name(description),
                description.Flags,
                static_cast<unsigned long long>(description.DedicatedVideoMemory / (1024 * 1024)));
        }
        adapter.Reset();
    }
}

void probe_d3d12_feature_level(D3D_FEATURE_LEVEL level)
{
    std::printf("\n[D3D12 feature level %s]\n", feature_level_name(level));
    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, level, IID_PPV_ARGS(&device));
    print_hresult("D3D12CreateDevice", hr);
    if (FAILED(hr) || !device)
        return;

    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    std::array<D3D_FEATURE_LEVEL, 5> requested{
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    levels.NumFeatureLevels = static_cast<UINT>(requested.size());
    levels.pFeatureLevelsRequested = requested.data();
    hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
    print_hresult("CheckFeatureSupport(FEATURE_LEVELS)", hr);
    std::printf("  Maximum supported feature level: %s\n",
        hr == S_OK ? feature_level_name(levels.MaxSupportedFeatureLevel) : "unknown");

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    print_hresult("CheckFeatureSupport(OPTIONS)", hr);
    if (SUCCEEDED(hr))
        std::printf("  Resource binding tier=%u, tiled resources tier=%u\n",
            static_cast<unsigned int>(options.ResourceBindingTier),
            static_cast<unsigned int>(options.TiledResourcesTier));

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
    rootSignatureDescription.NumParameters = 0;
    rootSignatureDescription.NumStaticSamplers = 0;
    rootSignatureDescription.pParameters = nullptr;
    rootSignatureDescription.pStaticSamplers = nullptr;
    rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> rootSignatureErrors;
    hr = D3D12SerializeRootSignature(
        &rootSignatureDescription,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &rootSignatureBlob,
        &rootSignatureErrors);
    print_hresult("SerializeRootSignature 1.0", hr);
    if (SUCCEEDED(hr))
    {
        ComPtr<ID3D12RootSignature> rootSignature;
        hr = device->CreateRootSignature(
            0,
            rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature));
        print_hresult("CreateRootSignature 1.0", hr);
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.NumParameters = 0;
    versioned.Desc_1_1.NumStaticSamplers = 0;
    versioned.Desc_1_1.pParameters = nullptr;
    versioned.Desc_1_1.pStaticSamplers = nullptr;
    versioned.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    rootSignatureBlob.Reset();
    rootSignatureErrors.Reset();
    hr = D3D12SerializeVersionedRootSignature(
        &versioned,
        &rootSignatureBlob,
        &rootSignatureErrors);
    print_hresult("SerializeVersionedRootSignature 1.1", hr);
    if (SUCCEEDED(hr))
    {
        ComPtr<ID3D12RootSignature> rootSignature;
        hr = device->CreateRootSignature(
            0,
            rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature));
        print_hresult("CreateRootSignature 1.1", hr);
    }

    HRESULT removedReason = device->GetDeviceRemovedReason();
    print_hresult("GetDeviceRemovedReason", removedReason);

    probe_exact_app_pipeline(device.Get(), level);
    if (level == D3D_FEATURE_LEVEL_11_0)
        probe_exact_composition_swap_chain(device.Get());
}

void probe_d3d11()
{
    std::printf("\n[D3D11 HARDWARE DEVICE]\n");
    std::array<D3D_FEATURE_LEVEL, 6> featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_1
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL actual = D3D_FEATURE_LEVEL_9_1;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels.data(),
        static_cast<UINT>(featureLevels.size()),
        D3D11_SDK_VERSION,
        &device,
        &actual,
        &context);
    print_hresult("D3D11CreateDevice(HARDWARE)", hr);
    if (SUCCEEDED(hr) && device)
        std::printf("  Actual D3D11 feature level: %s\n", feature_level_name(actual));
}
}

int wmain()
{
    std::printf("Windows Direct3D Compatibility Probe\n");
    probe_d3d12_adapters();

    std::array<D3D_FEATURE_LEVEL, 5> levels{
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    for (D3D_FEATURE_LEVEL level : levels)
        probe_d3d12_feature_level(level);

    probe_d3d11();
    return 0;
}
