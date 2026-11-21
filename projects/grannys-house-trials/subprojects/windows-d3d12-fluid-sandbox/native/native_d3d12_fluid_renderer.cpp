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
#include <limits>

#include "native_d3d12_diagnostics.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::uint32_t kFieldWidth = 192;
constexpr std::uint32_t kFieldHeight = 128;
constexpr std::uint32_t kRenderScale = 2;
constexpr std::uint32_t kRenderWidth = kFieldWidth * kRenderScale;
constexpr std::uint32_t kRenderHeight = kFieldHeight * kRenderScale;
constexpr float kProjectionHeightRange = 128.0f;
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

    void get_render_size(std::uint32_t& width, std::uint32_t& height) const noexcept
    {
        width = backbuffer_width_;
        height = backbuffer_height_;
    }

    void reset_state()
    {
        terrain_.resize(kFieldWidth * kFieldHeight);
        initial_terrain_.resize(kFieldWidth * kFieldHeight);
        water_.assign(kFieldWidth * kFieldHeight, 0.0f);
        spring_rates_.assign(kFieldWidth * kFieldHeight, 0.0f);
        preview_cells_.assign(kFieldWidth * kFieldHeight, 0);

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
        if (!normalized_to_field(normalizedX, normalizedY, fieldX, fieldY))
            return;
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
        if (!normalized_to_field(normalizedX, normalizedY, fieldX, fieldY))
            return;
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

    void set_view(
        float panX,
        float panY,
        float zoom,
        std::uint32_t viewMode,
        std::uint32_t displayMode)
    {
        view_pan_x_ = std::clamp(panX, -static_cast<float>(kFieldWidth), 2.0f * kFieldWidth);
        view_pan_y_ = std::clamp(panY, -static_cast<float>(kFieldHeight), 2.0f * kFieldHeight);
        view_zoom_ = std::clamp(zoom, 0.4f, 12.0f);
        view_mode_ = std::min(viewMode, 1u);
        display_mode_ = std::min(displayMode, 2u);
        render();
    }

    void set_frame(const std::int32_t* terrain, const float* water,
        const float* springs, std::uint32_t count, bool resetSpan)
    {
        const std::size_t expected = kFieldWidth * kFieldHeight;
        if (!terrain || !water || !springs || count != expected)
            throw static_cast<HRESULT>(E_INVALIDARG);
        for (std::size_t i = 0; i < expected; ++i)
        {
            if (!std::isfinite(water[i]) || water[i] < 0.0f ||
                !std::isfinite(springs[i]) || springs[i] < 0.0f)
                throw static_cast<HRESULT>(E_INVALIDARG);
            terrain_[i] = std::clamp(terrain[i], 0, 4096);
            water_[i] = water[i];
            spring_rates_[i] = springs[i];
        }
        if (resetSpan)
            initial_terrain_ = terrain_;
        if (cpu_reference_enabled_)
            cpu_water_ = water_;
        render();
    }

    void set_orbit(float yawDegrees, float elevationDegrees, float rollDegrees, float panZ)
    {
        view_yaw_degrees_ = windows_d3d12_fluid::normalized_view_yaw(yawDegrees);
        view_elevation_degrees_ = windows_d3d12_fluid::clamped_view_elevation(elevationDegrees);
        view_roll_degrees_ = std::isfinite(rollDegrees) ? std::remainder(rollDegrees, 360.0f) : 0.0f;
        view_pan_z_ = std::clamp(std::isfinite(panZ) ? panZ : 0.0f, -128.0f, 256.0f);
        render();
    }

    void set_camera(float panX, float panY, float panZ, float zoom,
        float yawDegrees, float elevationDegrees, float rollDegrees,
        float anchorX, float anchorY)
    {
        if (!std::isfinite(panX) || !std::isfinite(panY) || !std::isfinite(panZ) ||
            !std::isfinite(zoom) || !std::isfinite(anchorX) || !std::isfinite(anchorY))
            throw static_cast<HRESULT>(E_INVALIDARG);
        view_pan_x_ = std::clamp(panX, -static_cast<float>(kFieldWidth), 2.0f * kFieldWidth);
        view_pan_y_ = std::clamp(panY, -static_cast<float>(kFieldHeight), 2.0f * kFieldHeight);
        view_pan_z_ = std::clamp(panZ, -128.0f, 256.0f);
        view_zoom_ = std::clamp(zoom, 0.4f, 12.0f);
        view_yaw_degrees_ = windows_d3d12_fluid::normalized_view_yaw(yawDegrees);
        view_elevation_degrees_ = windows_d3d12_fluid::clamped_view_elevation(elevationDegrees);
        view_roll_degrees_ = std::isfinite(rollDegrees) ? std::remainder(rollDegrees, 360.0f) : 0.0f;
        camera_anchor_x_ = std::clamp(anchorX, 0.0f, 1.0f);
        camera_anchor_y_ = std::clamp(anchorY, 0.0f, 1.0f);
        view_mode_ = 1;
        display_mode_ = 0;
        render();
    }

    void set_preview(const std::int32_t* cells, std::uint32_t count)
    {
        if (count > kFieldWidth * kFieldHeight || (count > 0 && !cells))
            throw static_cast<HRESULT>(E_INVALIDARG);
        preview_cells_.assign(kFieldWidth * kFieldHeight, 0);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (cells[i] < 0 || cells[i] >= static_cast<std::int32_t>(kFieldWidth * kFieldHeight))
                throw static_cast<HRESULT>(E_INVALIDARG);
            preview_cells_[static_cast<std::size_t>(cells[i])] = 1;
        }
        render();
    }

    void set_selected_cell(std::int32_t x, std::int32_t y)
    {
        if (x < -1 || x >= static_cast<std::int32_t>(kFieldWidth) ||
            y < -1 || y >= static_cast<std::int32_t>(kFieldHeight) ||
            ((x < 0) != (y < 0)))
            throw static_cast<HRESULT>(E_INVALIDARG);
        selected_x_ = x;
        selected_y_ = y;
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
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        float surfaceHeight = 0.0f;
        if (!pick_cell(normalizedX, normalizedY, x, y, surfaceHeight))
        {
            if (selected_x_ >= 0)
            {
                selected_x_ = -1;
                selected_y_ = -1;
                render();
            }
            return;
        }
        if (selected_x_ == static_cast<std::int32_t>(x) &&
            selected_y_ == static_cast<std::int32_t>(y))
            return;
        selected_x_ = static_cast<std::int32_t>(x);
        selected_y_ = static_cast<std::int32_t>(y);
        render();
    }

    bool pick_point(
        float normalizedX,
        float normalizedY,
        float& fieldX,
        float& fieldY,
        float& surfaceHeight) const
    {
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
            normalizedX < 0.0f || normalizedX > 1.0f ||
            normalizedY < 0.0f || normalizedY > 1.0f)
            return false;
        const auto pixelX = std::min(
            static_cast<std::uint32_t>(normalizedX * kRenderWidth),
            kRenderWidth - 1);
        const auto pixelY = std::min(
            static_cast<std::uint32_t>(normalizedY * kRenderHeight),
            kRenderHeight - 1);
        const std::size_t pixel = static_cast<std::size_t>(pixelY) * kRenderWidth + pixelX;
        const std::int32_t cell = pick_cells_[pixel];
        if (cell < 0)
            return false;
        fieldX = pick_world_x_[pixel];
        fieldY = pick_world_y_[pixel];
        surfaceHeight = pick_world_z_[pixel];
        return true;
    }

    bool pick_cell(float normalizedX, float normalizedY, std::uint32_t& x,
        std::uint32_t& y, float& surfaceHeight) const
    {
        float fieldX = 0.0f, fieldY = 0.0f;
        if (!pick_point(normalizedX, normalizedY, fieldX, fieldY, surfaceHeight))
            return false;
        const auto pixelX = std::min(static_cast<std::uint32_t>(normalizedX * kRenderWidth), kRenderWidth - 1);
        const auto pixelY = std::min(static_cast<std::uint32_t>(normalizedY * kRenderHeight), kRenderHeight - 1);
        const auto cell = pick_cells_[static_cast<std::size_t>(pixelY) * kRenderWidth + pixelX];
        if (cell < 0) return false;
        x = static_cast<std::uint32_t>(cell % static_cast<std::int32_t>(kFieldWidth));
        y = static_cast<std::uint32_t>(cell / static_cast<std::int32_t>(kFieldWidth));
        return true;
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
    bool normalized_to_field(
        float normalizedX,
        float normalizedY,
        float& fieldX,
        float& fieldY) const
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        float surfaceHeight = 0.0f;
        if (!pick_cell(normalizedX, normalizedY, x, y, surfaceHeight))
            return false;
        fieldX = static_cast<float>(x);
        fieldY = static_cast<float>(y);
        return true;
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
        textureDescription.Width = kRenderWidth;
        textureDescription.Height = kRenderHeight;
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
        const std::uint64_t rowPitch = (kRenderWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        const std::uint64_t uploadSize = rowPitch * kRenderHeight;
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
        pipelineDescription.RasterizerState.FrontCounterClockwise = FALSE;
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
        struct Color
        {
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
        };
        struct ScreenVertex
        {
            float x;
            float y;
            float depth;
            float worldX;
            float worldY;
            float worldZ;
        };

        constexpr Color background{ 8, 12, 18 };
        const std::size_t pixelCount = kRenderWidth * kRenderHeight;
        std::vector<std::uint8_t> image(pixelCount * 4);
        std::vector<float> depthBuffer(pixelCount, std::numeric_limits<float>::lowest());
        pick_cells_.assign(pixelCount, -1);
        pick_heights_.assign(pixelCount, 0.0f);
        pick_world_x_.assign(pixelCount, 0.0f);
        pick_world_y_.assign(pixelCount, 0.0f);
        pick_world_z_.assign(pixelCount, 0.0f);
        preview_cells_.resize(pixelCount, 0);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            image[i * 4 + 0] = background.r;
            image[i * 4 + 1] = background.g;
            image[i * 4 + 2] = background.b;
            image[i * 4 + 3] = 255;
        }

        const float yaw = view_yaw_degrees_ * 3.14159265358979323846f / 180.0f;
        const float elevation = view_elevation_degrees_ * 3.14159265358979323846f / 180.0f;
        const float rightX0 = std::cos(yaw);
        const float rightY0 = std::sin(yaw);
        const float elevationSin = std::sin(elevation);
        const float elevationCos = std::cos(elevation);
        const float upX0 = rightY0 * elevationSin;
        const float upY0 = -rightX0 * elevationSin;
        const float upZ0 = elevationCos;
        const float roll = view_roll_degrees_ * 3.14159265358979323846f / 180.0f;
        const float rollCos = std::cos(roll);
        const float rollSin = std::sin(roll);
        const bool projectedView = view_mode_ == 1;
        const float rightX = projectedView ? rightX0 * rollCos + upX0 * rollSin : rightX0;
        const float rightY = projectedView ? rightY0 * rollCos + upY0 * rollSin : rightY0;
        const float rightZ = projectedView ? upZ0 * rollSin : 0.0f;
        const float upX = projectedView ? -rightX0 * rollSin + upX0 * rollCos : rightY0;
        const float upY = projectedView ? -rightY0 * rollSin + upY0 * rollCos : -rightX0;
        const float upZ = projectedView ? upZ0 * rollCos : 0.0f;
        const float forwardX = projectedView ? -std::sin(yaw) * elevationCos : -rightY0;
        const float forwardY = projectedView ? std::cos(yaw) * elevationCos : rightX0;
        const float forwardZ = projectedView ? elevationSin : 0.0f;
        const auto [minimumTerrain, maximumTerrain] = std::minmax_element(
            initial_terrain_.begin(), initial_terrain_.end());
        const float span = static_cast<float>(std::max(kFieldWidth, kFieldHeight))
            + static_cast<float>(*maximumTerrain - *minimumTerrain) * 0.35f;
        const float pixelsPerCell = std::min(
            static_cast<float>(backbuffer_width_),
            static_cast<float>(backbuffer_height_) * 1.45f) * 0.88f / std::max(1.0f, span) * view_zoom_;
        const float virtualScaleX = static_cast<float>(kRenderWidth) / backbuffer_width_;
        const float virtualScaleY = static_cast<float>(kRenderHeight) / backbuffer_height_;

        const auto terrainColor = [](float terrain) -> Color
        {
            return {
                static_cast<std::uint8_t>(std::clamp(72.0f + terrain * 0.8f, 72.0f, 150.0f)),
                static_cast<std::uint8_t>(std::clamp(86.0f + terrain * 0.45f, 86.0f, 138.0f)),
                static_cast<std::uint8_t>(std::clamp(78.0f + terrain * 0.25f, 78.0f, 110.0f))
            };
        };
        const auto tint = [](Color color, float r, float g, float b) -> Color
        {
            return {
                static_cast<std::uint8_t>(std::clamp(color.r * r, 0.0f, 255.0f)),
                static_cast<std::uint8_t>(std::clamp(color.g * g, 0.0f, 255.0f)),
                static_cast<std::uint8_t>(std::clamp(color.b * b, 0.0f, 255.0f))
            };
        };
        const auto highlight = [](Color color) -> Color
        {
            return {
                static_cast<std::uint8_t>(std::min(255, color.r + 90)),
                static_cast<std::uint8_t>(std::min(255, color.g + 60)),
                static_cast<std::uint8_t>(std::max(0, color.b - 30))
            };
        };
        const auto topDownColor = [&](std::size_t index) -> Color
        {
            const float terrain = static_cast<float>(terrain_[index]);
            const float water = std::max(0.0f, water_[index]);
            Color ground = terrainColor(terrain);
            if (display_mode_ == 2)
                ground = tint(ground, 0.34f, 0.38f, 0.48f);
            if (display_mode_ != 1 && water > kMinimumWater)
            {
                const float blend = display_mode_ == 2
                    ? std::clamp(0.45f + water / 18.0f, 0.45f, 1.0f)
                    : std::clamp(water / 12.0f, 0.0f, 1.0f);
                ground = {
                    static_cast<std::uint8_t>(ground.r * (1.0f - blend) + 32.0f * blend),
                    static_cast<std::uint8_t>(ground.g * (1.0f - blend) + 118.0f * blend),
                    static_cast<std::uint8_t>(ground.b * (1.0f - blend) + 220.0f * blend)
                };
            }
            if (preview_cells_[index])
                return { 255, 180, 84 };
            if (static_cast<std::int32_t>(index % kFieldWidth) == selected_x_ &&
                static_cast<std::int32_t>(index / kFieldWidth) == selected_y_)
                return highlight(ground);
            if (spring_rates_[index] > 0.0f)
                return tint(ground, 0.55f, 1.25f, 0.95f);
            return ground;
        };

        if (view_mode_ == 0)
        {
            for (std::uint32_t y = 0; y < kRenderHeight; ++y)
            {
                for (std::uint32_t x = 0; x < kRenderWidth; ++x)
                {
                    const float screenX =
                        (static_cast<float>(x) + 0.5f - kRenderWidth * 0.5f) /
                        (pixelsPerCell * virtualScaleX);
                    const float screenY =
                        (static_cast<float>(y) + 0.5f - kRenderHeight * 0.5f) /
                        (pixelsPerCell * virtualScaleY);
                    // Match the projected view: increasing field Y moves down the screen.
                    const float worldX = view_pan_x_ + rightX * screenX + forwardX * screenY;
                    const float worldY = view_pan_y_ + rightY * screenX + forwardY * screenY;
                    if (worldX < 0.0f || worldY < 0.0f ||
                        worldX >= kFieldWidth || worldY >= kFieldHeight)
                        continue;
                    const auto cellX = static_cast<std::uint32_t>(worldX);
                    const auto cellY = static_cast<std::uint32_t>(worldY);
                    const std::size_t cellIndex = cellY * kFieldWidth + cellX;
                    const std::size_t pixel = y * kRenderWidth + x;
                    const Color color = topDownColor(cellIndex);
                    image[pixel * 4 + 0] = color.r;
                    image[pixel * 4 + 1] = color.g;
                    image[pixel * 4 + 2] = color.b;
                    pick_cells_[pixel] = static_cast<std::int32_t>(cellIndex);
                    pick_heights_[pixel] = static_cast<float>(terrain_[cellIndex])
                        + (display_mode_ == 1 ? 0.0f : std::max(0.0f, water_[cellIndex]));
                    pick_world_x_[pixel] = worldX;
                    pick_world_y_[pixel] = worldY;
                    pick_world_z_[pixel] = pick_heights_[pixel];
                }
            }
        }
        else
        {
            const auto project = [&](float x, float y, float z) -> ScreenVertex
            {
                const float dx = x - view_pan_x_;
                const float dy = y - view_pan_y_;
                const float dz = z - view_pan_z_;
                const float right = dx * rightX + dy * rightY + dz * rightZ;
                const float up = dx * upX + dy * upY + dz * upZ;
                const float depth = dx * forwardX + dy * forwardY + dz * forwardZ;
                return {
                    camera_anchor_x_ * kRenderWidth + right * pixelsPerCell * virtualScaleX,
                    camera_anchor_y_ * kRenderHeight - up * pixelsPerCell * virtualScaleY,
                    depth,
                    x, y, z
                };
            };
            const auto drawTriangle = [&](
                const ScreenVertex& a,
                const ScreenVertex& b,
                const ScreenVertex& c,
                Color color,
                std::int32_t cell,
                float surfaceHeight)
            {
                const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::abs(area) < 0.0001f)
                    return;
                const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
                const int maxX = std::min(static_cast<int>(kRenderWidth) - 1,
                    static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
                const int maxY = std::min(static_cast<int>(kRenderHeight) - 1,
                    static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        const float px = x + 0.5f;
                        const float py = y + 0.5f;
                        const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                        const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f)
                            continue;
                        const float depth = w0 * a.depth + w1 * b.depth + w2 * c.depth;
                        const std::size_t pixel = static_cast<std::size_t>(y) * kRenderWidth + x;
                        if (depth <= depthBuffer[pixel])
                            continue;
                        depthBuffer[pixel] = depth;
                        image[pixel * 4 + 0] = color.r;
                        image[pixel * 4 + 1] = color.g;
                        image[pixel * 4 + 2] = color.b;
                        pick_cells_[pixel] = cell;
                        pick_heights_[pixel] = surfaceHeight;
                        pick_world_x_[pixel] = w0 * a.worldX + w1 * b.worldX + w2 * c.worldX;
                        pick_world_y_[pixel] = w0 * a.worldY + w1 * b.worldY + w2 * c.worldY;
                        pick_world_z_[pixel] = w0 * a.worldZ + w1 * b.worldZ + w2 * c.worldZ;
                    }
                }
            };
            const auto drawQuad = [&](
                const ScreenVertex& a,
                const ScreenVertex& b,
                const ScreenVertex& c,
                const ScreenVertex& d,
                Color color,
                std::int32_t cell,
                float surfaceHeight)
            {
                drawTriangle(a, b, c, color, cell, surfaceHeight);
                drawTriangle(a, c, d, color, cell, surfaceHeight);
            };
            const auto drawColumn = [&](
                std::uint32_t x,
                std::uint32_t y,
                float lower,
                float upper,
                Color topColor,
                Color sideColor,
                std::int32_t cell)
            {
                if (upper <= lower)
                    return;
                const auto p00 = project(static_cast<float>(x), static_cast<float>(y), upper);
                const auto p10 = project(static_cast<float>(x + 1), static_cast<float>(y), upper);
                const auto p11 = project(static_cast<float>(x + 1), static_cast<float>(y + 1), upper);
                const auto p01 = project(static_cast<float>(x), static_cast<float>(y + 1), upper);
                drawQuad(p00, p10, p11, p01, topColor, cell, upper);

                if (forwardX > 0.0001f)
                    drawQuad(
                        project(static_cast<float>(x + 1), static_cast<float>(y), lower),
                        project(static_cast<float>(x + 1), static_cast<float>(y + 1), lower),
                        p11, p10, sideColor, cell, upper);
                else if (forwardX < -0.0001f)
                    drawQuad(
                        project(static_cast<float>(x), static_cast<float>(y + 1), lower),
                        project(static_cast<float>(x), static_cast<float>(y), lower),
                        p00, p01, sideColor, cell, upper);

                if (forwardY > 0.0001f)
                    drawQuad(
                        project(static_cast<float>(x), static_cast<float>(y + 1), lower),
                        project(static_cast<float>(x + 1), static_cast<float>(y + 1), lower),
                        p11, p01, sideColor, cell, upper);
                else if (forwardY < -0.0001f)
                    drawQuad(
                        project(static_cast<float>(x + 1), static_cast<float>(y), lower),
                        project(static_cast<float>(x), static_cast<float>(y), lower),
                        p00, p10, sideColor, cell, upper);
            };

            for (std::uint32_t y = 0; y < kFieldHeight; ++y)
            {
                for (std::uint32_t x = 0; x < kFieldWidth; ++x)
                {
                    const std::size_t index = y * kFieldWidth + x;
                    const auto cell = static_cast<std::int32_t>(index);
                    const float terrainInches = static_cast<float>(std::max(0, terrain_[index]));
                    const float waterInches = std::max(0.0f, water_[index]);
                    const float terrain = terrainInches * 0.35f;
                    const float water = waterInches * 0.35f;
                    const bool selected = static_cast<std::int32_t>(x) == selected_x_ &&
                        static_cast<std::int32_t>(y) == selected_y_;
                    Color ground = terrainColor(terrain);
                    if (display_mode_ == 2)
                        ground = tint(ground, 0.34f, 0.38f, 0.48f);
                    else if (spring_rates_[index] > 0.0f)
                        ground = tint(ground, 0.55f, 1.25f, 0.95f);
                    if (selected)
                        ground = highlight(ground);
                    if (preview_cells_[index])
                        ground = { 255, 180, 84 };
                    drawColumn(x, y, 0.0f, terrain, ground, tint(ground, 0.62f, 0.64f, 0.72f), cell);

                    if (display_mode_ != 1 && water > kMinimumWater)
                    {
                        const float intensity = std::clamp(0.55f + waterInches / 24.0f, 0.55f, 1.0f);
                        Color waterColor{
                            static_cast<std::uint8_t>(32.0f * intensity),
                            static_cast<std::uint8_t>(118.0f * intensity),
                            static_cast<std::uint8_t>(220.0f * intensity)
                        };
                        if (selected)
                            waterColor = highlight(waterColor);
                        drawColumn(
                            x, y, terrain, terrain + water,
                            waterColor, tint(waterColor, 0.62f, 0.68f, 0.78f), cell);
                    }
                }
            }
        }

        void* mapped = nullptr;
        HRESULT hr = field_upload->Map(0, nullptr, &mapped);
        record_initialization_observation(
            windows_d3d12_fluid::InitializationStage::reset_texture_upload,
            hr);
        throw_if_failed(hr);
        auto* pixels = static_cast<unsigned char*>(mapped);
        const std::uint64_t rowPitch = (kRenderWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        for (std::uint32_t y = 0; y < kRenderHeight; ++y)
        {
            std::memcpy(
                pixels + static_cast<std::size_t>(y) * rowPitch,
                image.data() + static_cast<std::size_t>(y) * kRenderWidth * 4,
                kRenderWidth * 4);
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
        const std::uint64_t rowPitch = (kRenderWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        sourceLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sourceLocation.PlacedFootprint.Footprint.Width = kRenderWidth;
        sourceLocation.PlacedFootprint.Footprint.Height = kRenderHeight;
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
    std::vector<std::int32_t> pick_cells_ =
        std::vector<std::int32_t>(kFieldWidth * kFieldHeight, -1);
    std::vector<float> pick_heights_ =
        std::vector<float>(kFieldWidth * kFieldHeight, 0.0f);
    std::vector<float> pick_world_x_ =
        std::vector<float>(kFieldWidth * kFieldHeight, 0.0f);
    std::vector<float> pick_world_y_ =
        std::vector<float>(kFieldWidth * kFieldHeight, 0.0f);
    std::vector<float> pick_world_z_ =
        std::vector<float>(kFieldWidth * kFieldHeight, 0.0f);
    std::vector<float> water_;
    std::vector<float> spring_rates_;
    std::vector<std::uint8_t> preview_cells_;
    std::vector<float> cpu_water_;
    bool cpu_reference_enabled_ = false;
    float native_step_ms_per_tick_ = 0.0f;
    float cpu_step_ms_per_tick_ = 0.0f;
    float draw_ms_ = 0.0f;
    float view_pan_x_ = static_cast<float>(kFieldWidth) / 2.0f;
    float view_pan_y_ = static_cast<float>(kFieldHeight) / 2.0f;
    float view_zoom_ = 1.0f;
    float view_yaw_degrees_ = 0.0f;
    float view_elevation_degrees_ = 80.0f;
    float view_roll_degrees_ = 0.0f;
    float view_pan_z_ = 64.0f;
    float camera_anchor_x_ = 0.5f;
    float camera_anchor_y_ = 0.5f;
    std::uint32_t view_mode_ = 0;
    std::uint32_t display_mode_ = 0;
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
    std::uint32_t viewMode,
    std::uint32_t displayMode)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_view(panX, panY, zoom, viewMode, displayMode);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetFrame(
    const std::int32_t* terrain,
    const float* water,
    const float* springs,
    std::uint32_t count,
    int resetSpan)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_frame(terrain, water, springs, count, resetSpan != 0);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetCamera(
    float panX, float panY, float panZ, float zoom,
    float yawDegrees, float elevationDegrees, float rollDegrees,
    float anchorX, float anchorY)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_camera(panX, panY, panZ, zoom, yawDegrees,
            elevationDegrees, rollDegrees, anchorX, anchorY);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetPreview(
    const std::int32_t* cells, std::uint32_t count)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_preview(cells, count);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetSelectedCell(
    std::int32_t x, std::int32_t y)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_selected_cell(x, y);
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

extern "C" __declspec(dllexport) int __cdecl NativeSetOrbit(
    float yawDegrees,
    float elevationDegrees,
    float rollDegrees,
    float panZ)
{
    if (!gRenderer)
        return E_POINTER;
    try
    {
        gRenderer->set_orbit(yawDegrees, elevationDegrees, rollDegrees, panZ);
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

extern "C" __declspec(dllexport) int __cdecl NativePickCell(
    float normalizedX,
    float normalizedY,
    std::uint32_t* x,
    std::uint32_t* y,
    float* surfaceHeight)
{
    if (!gRenderer || !x || !y || !surfaceHeight)
        return E_POINTER;
    return gRenderer->pick_cell(normalizedX, normalizedY, *x, *y, *surfaceHeight)
        ? S_OK
        : S_FALSE;
}

extern "C" __declspec(dllexport) int __cdecl NativePickPoint(
    float normalizedX,
    float normalizedY,
    float* fieldX,
    float* fieldY,
    float* surfaceHeight)
{
    if (!gRenderer || !fieldX || !fieldY || !surfaceHeight)
        return E_POINTER;
    return gRenderer->pick_point(normalizedX, normalizedY, *fieldX, *fieldY, *surfaceHeight)
        ? S_OK
        : S_FALSE;
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

extern "C" __declspec(dllexport) int __cdecl NativeGetRenderSize(
    std::uint32_t* width, std::uint32_t* height)
{
    if (!gRenderer || !width || !height)
        return E_POINTER;
    gRenderer->get_render_size(*width, *height);
    return 0;
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
