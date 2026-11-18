#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <unknwn.h>
#include <windows.ui.xaml.media.dxinterop.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>
#include <chrono>

#include "native_d3d12_diagnostics.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::uint32_t kFieldWidth = 192;
constexpr std::uint32_t kFieldHeight = 128;
constexpr std::uint32_t kFrameCount = 2;
constexpr double kMaxFlowInches = 2.0;
constexpr double kSettleRate = 0.5;
constexpr double kMinimumWater = 0.001;

std::uint32_t gInitializationStage = 0;
ComPtr<ID3D12Device> gDiagnosticDevice;
windows_d3d12_fluid::InitializationReport gInitializationReport;

struct NativeStatus
{
    std::uint32_t Tick;
    std::uint32_t WetCells;
    float TotalWater;
    float MaximumDepth;
};

void throw_if_failed(HRESULT hr)
{
    if (FAILED(hr))
        throw hr;
}

std::vector<unsigned char> read_file(const wchar_t* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw static_cast<HRESULT>(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

void record_initialization_observation(
    windows_d3d12_fluid::InitializationStage stage,
    HRESULT hresult,
    const std::chrono::steady_clock::time_point& start)
{
    const HRESULT removedReason = gDiagnosticDevice
        ? gDiagnosticDevice->GetDeviceRemovedReason()
        : S_OK;
    const std::uint64_t elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
    gInitializationReport.record(
        stage,
        static_cast<std::int32_t>(hresult),
        static_cast<std::int32_t>(removedReason),
        elapsed);
}

void record_initialization_observation(
    windows_d3d12_fluid::InitializationStage stage,
    HRESULT hresult)
{
    const HRESULT removedReason = gDiagnosticDevice
        ? gDiagnosticDevice->GetDeviceRemovedReason()
        : S_OK;
    gInitializationReport.record(
        stage,
        static_cast<std::int32_t>(hresult),
        static_cast<std::int32_t>(removedReason),
        0);
}

class NativeD3D12FluidRenderer
{
public:
    NativeD3D12FluidRenderer(std::uint32_t width, std::uint32_t height, const wchar_t* shaderDirectory)
        : backbuffer_width_(std::max(width, 1u)), backbuffer_height_(std::max(height, 1u))
    {
        gInitializationReport.reset();
        const auto constructorStart = std::chrono::steady_clock::now();
        const HRESULT comResult = S_OK;
        gInitializationStage = 2;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::com_init,
            comResult,
            constructorStart);

        initialize_device();
        gInitializationStage = 3;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::d3d12_device,
            S_OK,
            constructorStart);

        initialize_swap_chain();
        gInitializationStage = 4;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::composition_swap_chain,
            S_OK,
            constructorStart);

        initialize_field_resources();
        gInitializationStage = 5;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::field_resources,
            S_OK,
            constructorStart);

        initialize_pipeline(shaderDirectory);
        gInitializationStage = 6;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::pipeline,
            S_OK,
            constructorStart);

        reset_state();
    }

    ~NativeD3D12FluidRenderer()
    {
        wait_for_gpu();
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        if (width == backbuffer_width_ && height == backbuffer_height_)
            return;

        wait_for_gpu();
        for (auto& target : render_targets_)
            target.Reset();

        throw_if_failed(swap_chain_->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        backbuffer_width_ = width;
        backbuffer_height_ = height;
        frame_index_ = 0;
        create_render_targets();
    }

    void get_swap_chain(IUnknown** swapChain)
    {
        if (!swapChain)
            throw static_cast<HRESULT>(E_INVALIDARG);

        ComPtr<IDXGISwapChain> dxgiSwapChain;
        throw_if_failed(swap_chain_.As(&dxgiSwapChain));
        *swapChain = dxgiSwapChain.Detach();
    }

    void reset_state()
    {
        terrain_.resize(kFieldWidth * kFieldHeight);
        water_.assign(kFieldWidth * kFieldHeight, 0.0f);

        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::uint32_t index = y * kFieldWidth + x;
                const float descent = static_cast<float>(kFieldHeight - y);
                const float farSide = static_cast<float>(kFieldWidth - x);
                terrain_[index] = static_cast<std::int32_t>(std::clamp(48.0f - descent * 0.18f - farSide * 0.03f, 12.0f, 48.0f));
            }
        }

        tick_ = 0;
    }

    void reset_field()
    {
        reset_state();
        render();
    }

    void pour(float inches)
    {
        const std::int32_t centerX = kFieldWidth / 2;
        const std::int32_t centerY = kFieldHeight / 3;
        const std::int32_t radius = 8;

        for (std::int32_t y = centerY - radius; y <= centerY + radius; ++y)
        {
            for (std::int32_t x = centerX - radius; x <= centerX + radius; ++x)
            {
                if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(kFieldWidth) || y >= static_cast<std::int32_t>(kFieldHeight))
                    continue;
                const std::int32_t dx = x - centerX;
                const std::int32_t dy = y - centerY;
                if (dx * dx + dy * dy <= radius * radius)
                    water_[static_cast<std::size_t>(y) * kFieldWidth + x] += inches;
            }
        }

        render();
    }

    void step()
    {
        std::vector<double> proposals(kFieldWidth * kFieldHeight * 5, 0.0);

        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::size_t index = y * kFieldWidth + x;
                const double available = water_[index];
                if (available <= kMinimumWater)
                    continue;

                const std::size_t count = kFieldWidth * kFieldHeight;
                std::array<std::uint32_t, 4> neighborIndex{};
                std::array<double, 4> neighborSurface{};
                std::uint32_t lowerCount = 0;
                const double currentSurface = terrain_[index] + available;
                double surfaceSum = currentSurface;

                const auto consider = [&](std::size_t neighbor)
                {
                    if (neighbor >= count)
                        return;
                    const double surface = terrain_[neighbor] + water_[neighbor];
                    if (surface < currentSurface - kMinimumWater)
                    {
                        neighborIndex[lowerCount] = static_cast<std::uint32_t>(neighbor);
                        neighborSurface[lowerCount] = surface;
                        surfaceSum += surface;
                        ++lowerCount;
                    }
                };

                if (x > 0)
                    consider(index - 1);
                if (x + 1 < kFieldWidth)
                    consider(index + 1);
                if (y > 0)
                    consider(index - kFieldWidth);
                if (y + 1 < kFieldHeight)
                    consider(index + kFieldWidth);

                if (lowerCount == 0)
                    continue;

                const double sharedSurface = surfaceSum / (lowerCount + 1);
                double desiredOutflow = 0.0;
                std::array<double, 4> desired{};
                for (std::uint32_t i = 0; i < lowerCount; ++i)
                {
                    desired[i] = std::max(0.0, sharedSurface - neighborSurface[i]);
                    desiredOutflow += desired[i];
                }
                if (desiredOutflow <= kMinimumWater)
                    continue;

                const double totalFlow = std::min(std::min(available, kMaxFlowInches),
                    desiredOutflow * kSettleRate);
                if (totalFlow <= kMinimumWater)
                    continue;

                const double scale = totalFlow / desiredOutflow;
                std::size_t proposal = index * 5;
                proposals[proposal] = totalFlow;
                for (std::uint32_t i = 0; i < lowerCount; ++i)
                {
                    const std::size_t neighbor = neighborIndex[i];
                    const std::uint32_t direction = [&]()
                    {
                        if (neighbor == index - 1)
                            return 1u;
                        if (neighbor == index + 1)
                            return 2u;
                        if (neighbor == index - kFieldWidth)
                            return 3u;
                        return 4u;
                    }();
                    proposals[proposal + direction] = desired[i] * scale;
                }
            }
        }

        std::vector<float> nextWater = water_;
        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::size_t index = y * kFieldWidth + x;
                const std::size_t proposal = index * 5;
                double delta = 0.0;
                if (x > 0)
                    delta += proposals[(index - 1) * 5 + 2];
                if (x + 1 < kFieldWidth)
                    delta += proposals[(index + 1) * 5 + 1];
                if (y > 0)
                    delta += proposals[(index - kFieldWidth) * 5 + 4];
                if (y + 1 < kFieldHeight)
                    delta += proposals[(index + kFieldWidth) * 5 + 3];
                delta -= proposals[proposal + 1];
                delta -= proposals[proposal + 2];
                delta -= proposals[proposal + 3];
                delta -= proposals[proposal + 4];
                nextWater[index] = static_cast<float>(std::max(0.0, water_[index] + delta));

                if (x == 0 || y == 0 || x + 1 == kFieldWidth || y + 1 == kFieldHeight)
                    nextWater[index] = 0.0f;
            }
        }

        water_ = std::move(nextWater);
        ++tick_;
        render();
    }

    NativeStatus status() const
    {
        NativeStatus result{};
        result.Tick = tick_;
        for (float depth : water_)
        {
            if (depth > kMinimumWater)
                ++result.WetCells;
            result.TotalWater += depth;
            result.MaximumDepth = std::max(result.MaximumDepth, depth);
        }
        return result;
    }

private:
    void initialize_device()
    {
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::dxgi_factory,
            hr);
        throw_if_failed(hr);

        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::d3d12_device,
            hr);
        throw_if_failed(hr);
        gDiagnosticDevice = device;

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        hr = device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::command_queue,
            hr);
        throw_if_failed(hr);

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::fence,
            hr);
        throw_if_failed(hr);
        fence_value_ = 1;
        fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_)
            throw static_cast<HRESULT>(E_FAIL);
    }

    void initialize_swap_chain()
    {
        gInitializationStage = 31;
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = backbuffer_width_;
        description.Height = backbuffer_height_;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = kFrameCount;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ComPtr<IDXGISwapChain1> swapChain;
        gInitializationStage = 32;
        HRESULT hr = factory->CreateSwapChainForComposition(queue.Get(), &description, nullptr, &swapChain);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::swap_chain_create,
            hr);
        throw_if_failed(hr);
        hr = swapChain.As(&swap_chain_);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::swap_chain_create,
            hr);
        throw_if_failed(hr);

        gInitializationStage = 35;
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.NumDescriptors = kFrameCount;
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hr = device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&rtv_heap));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::rtv_heap_create,
            hr);
        throw_if_failed(hr);
        rtv_descriptor_size_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        create_render_targets();

        for (std::uint32_t i = 0; i < kFrameCount; ++i)
        {
            throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
            allocators_[i] = allocator;
        }

        throw_if_failed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list)));
        throw_if_failed(command_list->Close());

        frame_index_ = 0;
    }

    void create_render_targets()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t i = 0; i < kFrameCount; ++i)
        {
            HRESULT hr = swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i]));
            record_initialization_observation(
                windows_d3d12_fluid::InitializationStage::backbuffers_create,
                hr);
            throw_if_failed(hr);
            device->CreateRenderTargetView(render_targets_[i].Get(), nullptr, handle);
            handle.ptr += rtv_descriptor_size_;
        }
    }

    void initialize_field_resources()
    {
        D3D12_RESOURCE_DESC textureDescription{};
        textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDescription.Width = kFieldWidth;
        textureDescription.Height = kFieldHeight;
        textureDescription.DepthOrArraySize = 1;
        textureDescription.MipLevels = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
            &textureDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&field_texture));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::field_texture_create,
            hr);
        throw_if_failed(hr);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        const std::uint64_t rowPitch = (kFieldWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const std::uint64_t uploadSize = rowPitch * kFieldHeight;
        D3D12_RESOURCE_DESC uploadDescription{};
        uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDescription.Width = uploadSize;
        uploadDescription.Height = 1;
        uploadDescription.DepthOrArraySize = 1;
        uploadDescription.MipLevels = 1;
        uploadDescription.Format = DXGI_FORMAT_UNKNOWN;
        uploadDescription.SampleDesc.Count = 1;
        uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
            &uploadDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&field_upload));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::field_upload_create,
            hr);
        throw_if_failed(hr);

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
        srvHeapDescription.NumDescriptors = 1;
        srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&srv_heap));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::srv_heap_create,
            hr);
        throw_if_failed(hr);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDescription.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(field_texture.Get(), &srvDescription,
            srv_heap->GetCPUDescriptorHandleForHeapStart());
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::srv_create,
            S_OK);
    }

    void initialize_pipeline(const wchar_t* shaderDirectory)
    {
        gInitializationStage = 51;
        D3D12_ROOT_PARAMETER parameters[1]{};
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        parameters[0].DescriptorTable.pDescriptorRanges = &range;

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
        rootSignatureDescription.NumParameters = 1;
        rootSignatureDescription.pParameters = parameters;
        rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> errorBlob;
        gInitializationStage = 511;
        const HRESULT serializeResult = D3D12SerializeRootSignature(
            &rootSignatureDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &rootSignatureBlob,
            &errorBlob);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::root_signature_serialize,
            serializeResult,
            std::chrono::steady_clock::now());
        throw_if_failed(serializeResult);

        gInitializationStage = 512;
        const HRESULT createResult = device->CreateRootSignature(
            0,
            rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&root_signature));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::root_signature_create,
            createResult,
            std::chrono::steady_clock::now());
        throw_if_failed(createResult);

        gInitializationStage = 52;
        std::wstring base(shaderDirectory);
        if (!base.empty() && base.back() != L'\\')
            base.push_back(L'\\');
        const std::vector<unsigned char> vertexBytes = read_file((base + L"field_render_vs.cso").c_str());
        const std::vector<unsigned char> pixelBytes = read_file((base + L"field_render_ps.cso").c_str());
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::shaders,
            S_OK);

        gInitializationStage = 53;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescription{};
        pipelineDescription.pRootSignature = root_signature.Get();
        pipelineDescription.VS = { vertexBytes.data(), vertexBytes.size() };
        pipelineDescription.PS = { pixelBytes.data(), pixelBytes.size() };
        pipelineDescription.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipelineDescription.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pipelineDescription.RasterizerState.FrontCounterClockwise = TRUE;
        pipelineDescription.RasterizerState.DepthClipEnable = TRUE;
        pipelineDescription.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipelineDescription.SampleMask = UINT_MAX;
        pipelineDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDescription.NumRenderTargets = 1;
        pipelineDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineDescription.SampleDesc.Count = 1;
        HRESULT hr = device->CreateGraphicsPipelineState(&pipelineDescription, IID_PPV_ARGS(&pipeline));
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::graphics_pso,
            hr);
        throw_if_failed(hr);
    }

    void update_field_texture()
    {
        void* mapped = nullptr;
        HRESULT hr = field_upload->Map(0, nullptr, &mapped);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_texture_upload,
            hr);
        throw_if_failed(hr);
        auto* pixels = static_cast<unsigned char*>(mapped);
        std::memset(pixels, 0, kFieldWidth * kFieldHeight * 4);

        const std::uint64_t rowPitch = (kFieldWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::size_t index = y * kFieldWidth + x;
                const float terrain = static_cast<float>(terrain_[index]);
                const float water = water_[index];

                float red = std::clamp(72.0f + terrain * 0.8f, 72.0f, 150.0f);
                float green = std::clamp(86.0f + terrain * 0.45f, 86.0f, 138.0f);
                float blue = std::clamp(78.0f + terrain * 0.25f, 78.0f, 110.0f);

                if (water > kMinimumWater)
                {
                    const float blend = std::clamp(water / 12.0f, 0.0f, 1.0f);
                    red = red * (1.0f - blend) + 32.0f * blend;
                    green = green * (1.0f - blend) + 118.0f * blend;
                    blue = blue * (1.0f - blend) + 220.0f * blend;
                }

                const std::size_t pixel = y * rowPitch + x * 4;
                pixels[pixel + 0] = static_cast<unsigned char>(std::clamp(red, 0.0f, 255.0f));
                pixels[pixel + 1] = static_cast<unsigned char>(std::clamp(green, 0.0f, 255.0f));
                pixels[pixel + 2] = static_cast<unsigned char>(std::clamp(blue, 0.0f, 255.0f));
                pixels[pixel + 3] = 255;
            }
        }
        field_upload->Unmap(0, nullptr);
    }

    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        if (windows_d3d12_fluid::is_redundant_resource_transition(
            static_cast<std::uint32_t>(before),
            static_cast<std::uint32_t>(after)))
            return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);
    }

    void render()
    {
        gInitializationStage = 71;
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_start,
            S_OK);

        update_field_texture();
        HRESULT hr = allocators_[frame_index_]->Reset();
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_command_record,
            hr);
        throw_if_failed(hr);
        hr = command_list->Reset(allocators_[frame_index_].Get(), pipeline.Get());
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_command_record,
            hr);
        throw_if_failed(hr);

        transition(field_texture.Get(), field_state_, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
        sourceLocation.pResource = field_upload.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        const std::uint64_t rowPitch = (kFieldWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        sourceLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sourceLocation.PlacedFootprint.Footprint.Width = kFieldWidth;
        sourceLocation.PlacedFootprint.Footprint.Height = kFieldHeight;
        sourceLocation.PlacedFootprint.Footprint.Depth = 1;
        sourceLocation.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

        D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
        destinationLocation.pResource = field_texture.Get();
        destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destinationLocation.SubresourceIndex = 0;
        command_list->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_texture_copy,
            S_OK);

        transition(field_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        field_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        transition(render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frame_index_) * rtv_descriptor_size_;
        command_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        const float clearColor[] = { 0.02f, 0.03f, 0.05f, 1.0f };
        command_list->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        command_list->SetPipelineState(pipeline.Get());
        command_list->SetGraphicsRootSignature(root_signature.Get());

        ID3D12DescriptorHeap* heaps[] = { srv_heap.Get() };
        command_list->SetDescriptorHeaps(1, heaps);
        command_list->SetGraphicsRootDescriptorTable(
            0, srv_heap->GetGPUDescriptorHandleForHeapStart());

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(backbuffer_width_);
        viewport.Height = static_cast<float>(backbuffer_height_);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        command_list->RSSetViewports(1, &viewport);

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(backbuffer_width_), static_cast<LONG>(backbuffer_height_) };
        command_list->RSSetScissorRects(1, &scissor);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->DrawInstanced(3, 1, 0, 0);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_draw,
            S_OK);

        transition(render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        hr = command_list->Close();
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_resource_barriers,
            hr);
        throw_if_failed(hr);

        ID3D12CommandList* lists[] = { command_list.Get() };
        queue->ExecuteCommandLists(1, lists);
        hr = swap_chain_->Present(1, 0);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_present,
            hr);
        throw_if_failed(hr);
        wait_for_gpu();
        frame_index_ = (frame_index_ + 1) % kFrameCount;
    }

    void wait_for_gpu()
    {
        if (!queue || !fence)
            return;
        const std::uint64_t value = fence_value_++;
        throw_if_failed(queue->Signal(fence.Get(), value));
        if (fence->GetCompletedValue() < value)
        {
            throw_if_failed(fence->SetEventOnCompletion(value, fence_event_));
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain1> swap_chain_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12Resource> render_targets_[kFrameCount];
    ComPtr<ID3D12CommandAllocator> allocators_[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<ID3D12Resource> field_texture;
    ComPtr<ID3D12Resource> field_upload;
    D3D12_RESOURCE_STATES field_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event_ = nullptr;
    std::uint64_t fence_value_ = 1;
    std::uint32_t rtv_descriptor_size_ = 0;
    std::uint32_t frame_index_ = 0;
    std::uint32_t backbuffer_width_;
    std::uint32_t backbuffer_height_;

    std::uint32_t tick_ = 0;
    std::vector<std::int32_t> terrain_;
    std::vector<float> water_;

    ComPtr<ID3D12CommandAllocator> allocator;
};

NativeD3D12FluidRenderer* gRenderer = nullptr;
}

extern "C" __declspec(dllexport) int __cdecl NativeInitialize(
    std::uint32_t width, std::uint32_t height, const wchar_t* shaderDirectory)
{
    try
    {
        gInitializationStage = 1;
        delete gRenderer;
        gRenderer = new NativeD3D12FluidRenderer(width, height, shaderDirectory);
        return 0;
    }
    catch (HRESULT hr)
    {
        const int diagnostic = static_cast<int>(hr);
        char message[128]{};
        std::snprintf(message, sizeof(message), "NativeD3D12FluidRenderer stage=%u hresult=0x%08X\n",
            gInitializationStage, static_cast<unsigned int>(diagnostic));
        OutputDebugStringA(message);
        std::printf("%s", message);
        if (gDiagnosticDevice)
        {
            const HRESULT removedReason = gDiagnosticDevice->GetDeviceRemovedReason();
            char removedMessage[128]{};
            std::snprintf(removedMessage, sizeof(removedMessage),
                "NativeD3D12FluidRenderer device_removed_reason=0x%08X\n",
                static_cast<unsigned int>(removedReason));
            OutputDebugStringA(removedMessage);
            std::printf("%s", removedMessage);
        }
        std::fflush(stdout);
        return diagnostic;
    }
    catch (...)
    {
        const int diagnostic = static_cast<int>(0x60000000u | gInitializationStage);
        char message[128]{};
        std::snprintf(message, sizeof(message), "NativeD3D12FluidRenderer stage=%u diagnostic=0x%08X\n",
            gInitializationStage, static_cast<unsigned int>(diagnostic));
        OutputDebugStringA(message);
        std::printf("%s", message);
        std::fflush(stdout);
        return diagnostic;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativeResize(std::uint32_t width, std::uint32_t height)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->resize(width, height);
        return 0;
    }
    catch (HRESULT hr)
    {
        return static_cast<int>(hr);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativeReset()
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->reset_field();
        return 0;
    }
    catch (HRESULT hr)
    {
        return static_cast<int>(hr);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativePour(float inches)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->pour(inches);
        return 0;
    }
    catch (HRESULT hr)
    {
        return static_cast<int>(hr);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativeStep()
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->step();
        return 0;
    }
    catch (HRESULT hr)
    {
        return static_cast<int>(hr);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativeGetStatus(NativeStatus* status)
{
    if (!gRenderer || !status)
        return E_POINTER;
    *status = gRenderer->status();
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl NativeGetSwapChain(IUnknown** swapChain)
{
    if (!gRenderer || !swapChain)
        return E_POINTER;
    try
    {
        gRenderer->get_swap_chain(swapChain);
        return 0;
    }
    catch (HRESULT hr)
    {
        return static_cast<int>(hr);
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) int __cdecl NativeGetDeviceRemovedReason()
{
    if (!gDiagnosticDevice)
        return E_POINTER;
    return static_cast<int>(gDiagnosticDevice->GetDeviceRemovedReason());
}

extern "C" __declspec(dllexport) int __cdecl NativeGetInitializationReport(
    char* buffer,
    std::uint32_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return E_INVALIDARG;
    const std::string report = gInitializationReport.text();
    if (report.size() >= bufferSize)
        return E_NOT_SUFFICIENT_BUFFER;
    std::memcpy(buffer, report.c_str(), report.size() + 1);
    return S_OK;
}

extern "C" __declspec(dllexport) int __cdecl NativeDispose()
{
    delete gRenderer;
    gRenderer = nullptr;
    return 0;
}
