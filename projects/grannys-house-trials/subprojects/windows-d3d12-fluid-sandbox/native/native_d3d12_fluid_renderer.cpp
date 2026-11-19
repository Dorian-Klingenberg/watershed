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
#include <cmath>
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
constexpr double kDefaultMaxFlow = 2.0;
constexpr double kDefaultSettleRate = 0.5;

std::uint32_t gInitializationStage = 0;
ComPtr<ID3D12Device> gDiagnosticDevice;
windows_d3d12_fluid::InitializationReport gInitializationReport;

struct NativeStatus
{
    std::uint32_t Tick;
    std::uint32_t WetCells;
    float TotalWater;
    float MaximumDepth;
    std::uint32_t CpuTick;
    std::uint32_t CpuWetCells;
    float CpuTotalWater;
    float CpuMaximumDepth;
    std::uint32_t MismatchCells;
    float MaximumDepthMismatch;
    std::uint32_t BatchSize;
    std::uint32_t CpuReferenceEnabled;
    float NativeStepMsPerTick;
    float CpuStepMsPerTick;
    float DrawMs;
};

struct NativeCellStatus
{
    std::uint32_t X;
    std::uint32_t Y;
    std::int32_t Terrain;
    float Water;
    float CpuWater;
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
        dispose();
    }

    void dispose() noexcept
    {
        try
        {
            wait_for_gpu();
        }
        catch (...)
        {
        }

        for (auto& target : render_targets_)
            target.Reset();
        for (auto& allocator : allocators_)
            allocator.Reset();

        command_list.Reset();
        field_texture.Reset();
        field_upload.Reset();
        srv_heap.Reset();
        rtv_heap.Reset();
        root_signature.Reset();
        pipeline.Reset();
        fence.Reset();
        swap_chain_.Reset();

        if (fence_event_)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
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
        initial_terrain_.resize(kFieldWidth * kFieldHeight);
        water_.assign(kFieldWidth * kFieldHeight, 0.0f);

        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::uint32_t index = y * kFieldWidth + x;
                const float channelX = kFieldWidth / 2.0f + 16.0f * std::sin(static_cast<float>(y) / 24.0f);
                const float bank = std::min(28.0f, std::abs(static_cast<float>(x) - channelX) * 0.65f);
                const float slope = std::max(0.0f, 88.0f - static_cast<float>(y)) * 0.35f
                    + std::max(0.0f, 40.0f - static_cast<float>(y)) * 0.3f;
                const float basinRim = y > 96 ? static_cast<float>(y - 96) * 0.325f : 0.0f;
                float barrier = 0.0f;
                if (y >= 74 && y <= 76 && x >= 60 && x <= 123)
                    barrier = (x >= 90 && x <= 96) ? 5.0f : 12.0f;
                terrain_[index] = static_cast<std::int32_t>(
                    std::round(slope + bank + basinRim + barrier));
            }
        }

        initial_terrain_ = terrain_;
        tick_ = 0;
        cpu_tick_ = 0;
        cpu_water_.assign(kFieldWidth * kFieldHeight, 0.0f);
        cpu_reference_enabled_ = false;
    }

    void reset_field()
    {
        terrain_ = initial_terrain_;
        water_.assign(kFieldWidth * kFieldHeight, 0.0f);
        tick_ = 0;
        cpu_tick_ = 0;
        batch_size_ = 1;
        native_step_ms_per_tick_ = 0.0f;
        cpu_step_ms_per_tick_ = 0.0f;
        if (cpu_reference_enabled_)
            cpu_water_ = water_;
        render();
    }

    void pour(float inches)
    {
        const std::int32_t centerX = kFieldWidth / 2;
        const std::int32_t centerY = kFieldHeight / 3;
        pour_at(static_cast<float>(centerX) / kFieldWidth,
            static_cast<float>(centerY) / kFieldHeight,
            inches);
    }

    void pour_at(float normalizedX, float normalizedY, float inches)
    {
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
            !std::isfinite(inches) || inches < 0.0f)
            throw static_cast<HRESULT>(E_INVALIDARG);

        float fieldX = 0.0f;
        float fieldY = 0.0f;
        normalized_to_field(normalizedX, normalizedY, fieldX, fieldY);
        const std::int32_t centerX = static_cast<std::int32_t>(std::round(fieldX));
        const std::int32_t centerY = static_cast<std::int32_t>(std::round(fieldY));
        constexpr std::int32_t radius = 8;

        for (std::int32_t y = centerY - radius; y <= centerY + radius; ++y)
        {
            for (std::int32_t x = centerX - radius; x <= centerX + radius; ++x)
            {
                if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(kFieldWidth) || y >= static_cast<std::int32_t>(kFieldHeight))
                    continue;
                const std::int32_t dx = x - centerX;
                const std::int32_t dy = y - centerY;
                if (dx * dx + dy * dy <= radius * radius)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * kFieldWidth + x;
                    water_[index] += inches;
                    if (cpu_reference_enabled_)
                        cpu_water_[index] += inches;
                }
            }
        }

        render();
    }

    void edit_terrain(float normalizedX, float normalizedY, bool raiseTerrain)
    {
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY))
            throw static_cast<HRESULT>(E_INVALIDARG);

        float fieldX = 0.0f;
        float fieldY = 0.0f;
        normalized_to_field(normalizedX, normalizedY, fieldX, fieldY);
        const std::int32_t centerX = static_cast<std::int32_t>(std::round(fieldX));
        const std::int32_t centerY = static_cast<std::int32_t>(std::round(fieldY));
        constexpr std::int32_t radius = 5;

        for (std::int32_t y = centerY - radius; y <= centerY + radius; ++y)
        {
            for (std::int32_t x = centerX - radius; x <= centerX + radius; ++x)
            {
                if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(kFieldWidth) || y >= static_cast<std::int32_t>(kFieldHeight))
                    continue;
                const std::int32_t dx = x - centerX;
                const std::int32_t dy = y - centerY;
                if (dx * dx + dy * dy > radius * radius)
                    continue;
                const std::size_t index = static_cast<std::size_t>(y) * kFieldWidth + x;
                terrain_[index] = raiseTerrain
                    ? std::clamp(terrain_[index] + 12, 0, 4096)
                    : std::clamp(terrain_[index] - 12, 0, 4096);
            }
        }

        render();
    }

    void set_cpu_reference_enabled(bool enabled)
    {
        cpu_reference_enabled_ = enabled;
        if (enabled)
        {
            cpu_water_ = water_;
            cpu_tick_ = tick_;
        }
        else
        {
            cpu_water_.assign(kFieldWidth * kFieldHeight, 0.0f);
            cpu_tick_ = 0;
        }
        render();
    }

    void set_view(float panX, float panY, float zoom, std::uint32_t mode)
    {
        view_pan_x_ = std::clamp(panX, 0.0f, static_cast<float>(kFieldWidth));
        view_pan_y_ = std::clamp(panY, 0.0f, static_cast<float>(kFieldHeight));
        view_zoom_ = std::clamp(zoom, 1.0f, 4.0f);
        view_mode_ = mode == 0 ? 0u : 1u;
        render();
    }

    void set_settings(float maxFlow, float settleRate)
    {
        if (!std::isfinite(maxFlow) || maxFlow < 0.01f || maxFlow > 12.0f ||
            !std::isfinite(settleRate) || settleRate < 0.01f || settleRate > 1.0f)
            throw static_cast<HRESULT>(E_INVALIDARG);
        max_flow_ = static_cast<double>(maxFlow);
        settle_rate_ = static_cast<double>(settleRate);
    }

    void set_selection(float normalizedX, float normalizedY)
    {
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY))
            return;
        float fieldX = 0.0f;
        float fieldY = 0.0f;
        normalized_to_field(normalizedX, normalizedY, fieldX, fieldY);
        selected_x_ = std::clamp(static_cast<std::int32_t>(std::round(fieldX)),
            0, static_cast<std::int32_t>(kFieldWidth - 1));
        selected_y_ = std::clamp(static_cast<std::int32_t>(std::round(fieldY)),
            0, static_cast<std::int32_t>(kFieldHeight - 1));
        render();
    }

    NativeCellStatus get_cell_status(std::uint32_t x, std::uint32_t y) const
    {
        if (x >= kFieldWidth || y >= kFieldHeight)
            throw static_cast<HRESULT>(E_INVALIDARG);
        const std::size_t index = y * kFieldWidth + x;
        return {
            x,
            y,
            terrain_[index],
            water_[index],
            cpu_reference_enabled_ ? cpu_water_[index] : 0.0f
        };
    }

    void step()
    {
        step_batch(1);
    }

    void step_batch(std::uint32_t ticks)
    {
        if (!windows_d3d12_fluid::is_valid_tick_batch(ticks))
            throw static_cast<HRESULT>(E_INVALIDARG);
        batch_size_ = ticks;

        const auto nativeStart = std::chrono::steady_clock::now();
        for (std::uint32_t i = 0; i < ticks; ++i)
            step_once(water_);
        tick_ += ticks;
        native_step_ms_per_tick_ = static_cast<float>(
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                std::chrono::steady_clock::now() - nativeStart).count()) / ticks;

        if (cpu_reference_enabled_)
        {
            const auto cpuStart = std::chrono::steady_clock::now();
            for (std::uint32_t i = 0; i < ticks; ++i)
                step_once(cpu_water_);
            cpu_tick_ += ticks;
            cpu_step_ms_per_tick_ = static_cast<float>(
                std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                    std::chrono::steady_clock::now() - cpuStart).count()) / ticks;
        }
        else
        {
            cpu_step_ms_per_tick_ = 0.0f;
        }

        render();
    }

private:
    void normalized_to_field(float normalizedX, float normalizedY, float& fieldX, float& fieldY) const
    {
        const float nx = std::clamp(normalizedX, 0.0f, 1.0f);
        const float ny = std::clamp(normalizedY, 0.0f, 1.0f);
        fieldX = static_cast<float>(windows_d3d12_fluid::normalized_field_coordinate(
            nx,
            view_pan_x_ / kFieldWidth,
            view_zoom_,
            kFieldWidth));
        fieldY = static_cast<float>(windows_d3d12_fluid::normalized_field_coordinate(
            ny,
            view_pan_y_ / kFieldHeight,
            view_zoom_,
            kFieldHeight));
    }

    void step_once(std::vector<float>& state)
    {
        std::vector<double> proposals(kFieldWidth * kFieldHeight * 5, 0.0);

        for (std::uint32_t y = 0; y < kFieldHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kFieldWidth; ++x)
            {
                const std::size_t index = y * kFieldWidth + x;
                const double available = state[index];
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
                    const double surface = terrain_[neighbor] + state[neighbor];
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

                const double totalFlow = std::min(std::min(available, max_flow_),
                    desiredOutflow * settle_rate_);
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

        std::vector<float> nextWater = state;
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
                nextWater[index] = static_cast<float>(std::max(0.0, state[index] + delta));

                if (x == 0 || y == 0 || x + 1 == kFieldWidth || y + 1 == kFieldHeight)
                    nextWater[index] = 0.0f;
            }
        }

        state = std::move(nextWater);
    }

public:
    NativeStatus status() const
    {
        NativeStatus result{};
        result.Tick = tick_;
        result.BatchSize = batch_size_;
        result.CpuReferenceEnabled = cpu_reference_enabled_ ? 1u : 0u;
        result.NativeStepMsPerTick = native_step_ms_per_tick_;
        result.CpuStepMsPerTick = cpu_reference_enabled_ ? cpu_step_ms_per_tick_ : 0.0f;
        result.DrawMs = draw_ms_;

        for (float depth : water_)
        {
            if (depth > kMinimumWater)
                ++result.WetCells;
            result.TotalWater += depth;
            result.MaximumDepth = std::max(result.MaximumDepth, depth);
        }

        result.CpuTick = cpu_reference_enabled_ ? cpu_tick_ : 0u;
        if (cpu_reference_enabled_)
        {
            for (float depth : cpu_water_)
            {
                if (depth > kMinimumWater)
                    ++result.CpuWetCells;
                result.CpuTotalWater += depth;
                result.CpuMaximumDepth = std::max(result.CpuMaximumDepth, depth);
            }
            for (std::size_t i = 0; i < water_.size(); ++i)
            {
                const float mismatch = std::abs(water_[i] - cpu_water_[i]);
                if (mismatch > 0.0001f)
                    ++result.MismatchCells;
                result.MaximumDepthMismatch = std::max(result.MaximumDepthMismatch, mismatch);
            }
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
                const std::size_t pixel = y * rowPitch + x * 4;
                float red = 8.0f;
                float green = 12.0f;
                float blue = 18.0f;

                float sourceX = static_cast<float>(x);
                float sourceY = static_cast<float>(y);
                if (view_zoom_ > 1.0f || view_pan_x_ != kFieldWidth / 2.0f ||
                    view_pan_y_ != kFieldHeight / 2.0f)
                {
                    sourceX = std::clamp(view_pan_x_ + (static_cast<float>(x) / kFieldWidth - 0.5f)
                        * kFieldWidth / view_zoom_, 0.0f, static_cast<float>(kFieldWidth - 1));
                    sourceY = std::clamp(view_pan_y_ + (static_cast<float>(y) / kFieldHeight - 0.5f)
                        * kFieldHeight / view_zoom_, 0.0f, static_cast<float>(kFieldHeight - 1));
                }

                if (view_mode_ == 0)
                {
                    const std::uint32_t sx = static_cast<std::uint32_t>(std::round(sourceX));
                    const std::uint32_t sy = static_cast<std::uint32_t>(std::round(sourceY));
                    const std::size_t index = sy * kFieldWidth + sx;
                    const float terrain = static_cast<float>(terrain_[index]);
                    const float water = water_[index];

                    red = std::clamp(72.0f + terrain * 0.8f, 72.0f, 150.0f);
                    green = std::clamp(86.0f + terrain * 0.45f, 86.0f, 138.0f);
                    blue = std::clamp(78.0f + terrain * 0.25f, 78.0f, 110.0f);

                    if (water > kMinimumWater)
                    {
                        const float blend = std::clamp(water / 12.0f, 0.0f, 1.0f);
                        red = red * (1.0f - blend) + 32.0f * blend;
                        green = green * (1.0f - blend) + 118.0f * blend;
                        blue = blue * (1.0f - blend) + 220.0f * blend;
                    }

                    if (sx == selected_x_ && sy == selected_y_)
                    {
                        red = std::clamp(red + 90.0f, 0.0f, 255.0f);
                        green = std::clamp(green + 60.0f, 0.0f, 255.0f);
                        blue = std::clamp(blue - 30.0f, 0.0f, 255.0f);
                    }
                }
                else
                {
                    const std::uint32_t sx = static_cast<std::uint32_t>(std::clamp(
                        view_pan_x_ + (static_cast<float>(x) / kFieldWidth - 0.5f)
                            * kFieldWidth / view_zoom_,
                        0.0f,
                        static_cast<float>(kFieldWidth - 1)));
                    const std::size_t index = (kFieldHeight / 2) * kFieldWidth + sx;
                    const float terrain = static_cast<float>(terrain_[index]);
                    const float water = water_[index];
                    const float scale = kFieldHeight / 110.0f;
                    const float groundTop = kFieldHeight - terrain * scale;
                    const float waterTop = kFieldHeight - (terrain + water) * scale;

                    if (static_cast<float>(y) >= groundTop)
                    {
                        red = std::clamp(72.0f + terrain * 0.8f, 72.0f, 150.0f);
                        green = std::clamp(86.0f + terrain * 0.45f, 86.0f, 138.0f);
                        blue = std::clamp(78.0f + terrain * 0.25f, 78.0f, 110.0f);
                    }
                    if (static_cast<float>(y) >= waterTop && static_cast<float>(y) < groundTop &&
                        water > kMinimumWater)
                    {
                        red = 32.0f;
                        green = 118.0f;
                        blue = 220.0f;
                    }

                    if (sx == selected_x_)
                    {
                        red = std::clamp(red + 60.0f, 0.0f, 255.0f);
                        green = std::clamp(green + 45.0f, 0.0f, 255.0f);
                    }
                }

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
        const auto drawStart = std::chrono::steady_clock::now();
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
        draw_ms_ = static_cast<float>(
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                std::chrono::steady_clock::now() - drawStart).count());
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
    std::uint32_t cpu_tick_ = 0;
    std::uint32_t batch_size_ = 1;
    std::vector<std::int32_t> terrain_;
    std::vector<std::int32_t> initial_terrain_;
    std::vector<float> water_;
    std::vector<float> cpu_water_;
    bool cpu_reference_enabled_ = false;
    float native_step_ms_per_tick_ = 0.0f;
    float cpu_step_ms_per_tick_ = 0.0f;
    float draw_ms_ = 0.0f;
    float view_pan_x_ = static_cast<float>(kFieldWidth) / 2.0f;
    float view_pan_y_ = static_cast<float>(kFieldHeight) / 2.0f;
    float view_zoom_ = 1.0f;
    std::uint32_t view_mode_ = 0;
    std::int32_t selected_x_ = static_cast<std::int32_t>(kFieldWidth) / 2;
    std::int32_t selected_y_ = static_cast<std::int32_t>(kFieldHeight) / 2;
    double max_flow_ = kDefaultMaxFlow;
    double settle_rate_ = kDefaultSettleRate;

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

extern "C" __declspec(dllexport) int __cdecl NativeStepBatch(std::uint32_t ticks)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->step_batch(ticks);
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

extern "C" __declspec(dllexport) int __cdecl NativePourAt(
    float normalizedX,
    float normalizedY,
    float inches)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->pour_at(normalizedX, normalizedY, inches);
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

extern "C" __declspec(dllexport) int __cdecl NativeEditTerrain(
    float normalizedX,
    float normalizedY,
    int raiseTerrain)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->edit_terrain(normalizedX, normalizedY, raiseTerrain != 0);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetCpuReference(int enabled)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_cpu_reference_enabled(enabled != 0);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetView(
    float panX,
    float panY,
    float zoom,
    std::uint32_t mode)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_view(panX, panY, zoom, mode);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetSettings(
    float maxFlow,
    float settleRate)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_settings(maxFlow, settleRate);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetSelection(
    float normalizedX,
    float normalizedY)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_selection(normalizedX, normalizedY);
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

extern "C" __declspec(dllexport) int __cdecl NativeGetCellStatus(
    std::uint32_t x,
    std::uint32_t y,
    NativeCellStatus* status)
{
    if (!gRenderer || !status)
        return E_POINTER;
    try
    {
        *status = gRenderer->get_cell_status(x, y);
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
    if (gRenderer)
    {
        gRenderer->dispose();
        delete gRenderer;
        gRenderer = nullptr;
    }
    gDiagnosticDevice.Reset();
    return 0;
}
