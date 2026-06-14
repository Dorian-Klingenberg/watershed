#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "grannys_house_trials/sim/grass_field.h"
#include "sim/simple_cellular_fluid_sim.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace sim = grannys_house_trials::sim;

namespace
{

constexpr int k_warmup_steps = 5;
constexpr int k_measurement_steps = 30;
constexpr int k_repetitions = 3;
constexpr UINT k_threads_per_group = 256;

void throw_if_failed(HRESULT hr, const char* message)
{
    if (FAILED(hr))
        throw std::runtime_error(message);
}

struct Scenario
{
    const char* name = "";
    bool rain = false;
    int radius = 0;
    float depth_inches = 0.0f;
};

struct FluidConstants
{
    std::uint32_t field_width = 0;
    std::uint32_t field_depth = 0;
    float max_flow_inches = 2.0f;
    float settle_rate = 0.5f;
    float minimum_water_inches = 0.001f;
    std::uint32_t drain_edges = 1;
    std::uint32_t pad0 = 0;
    std::uint32_t pad1 = 0;
};

static_assert(sizeof(FluidConstants) == 32);

struct FlowProposal
{
    float total_outflow = 0.0f;
    float send_left = 0.0f;
    float send_right = 0.0f;
    float send_up = 0.0f;
    float send_down = 0.0f;
};

static_assert(sizeof(FlowProposal) == 20);

struct GpuRunResult
{
    double kernel_ms_per_step = 0.0;
    double submission_ms_per_step = 0.0;
    std::vector<float> water;
};

[[nodiscard]] std::filesystem::path executable_directory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        throw std::runtime_error("GetModuleFileNameW failed.");

    return std::filesystem::path(path).parent_path();
}

[[nodiscard]] ComPtr<ID3DBlob> load_shader_blob(const wchar_t* filename)
{
    const std::filesystem::path path = executable_directory() / L"shaders" / filename;
    ComPtr<ID3DBlob> blob;
    throw_if_failed(D3DReadFileToBlob(path.c_str(), &blob), "D3DReadFileToBlob failed.");
    return blob;
}

[[nodiscard]] std::vector<int> build_live_seed_heights()
{
    sim::GrassField grass_field{100, 100, 1.0f};
    const int source_w = grass_field.width();
    const int source_d = grass_field.depth();
    const int detail = sim::GrassField::detail_patch_resolution();
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
                    heights.push_back(grass_field.fine_top_height_inches_at(
                        source_x, source_z, local_x, local_z));
                }
            }
        }
    }

    return heights;
}

void configure_baseline(
    sim::SimpleCellularFluidSim& fluid,
    const std::vector<int>& seed,
    int width,
    int depth,
    const Scenario& scenario)
{
    fluid.reset(width, depth, seed);
    if (scenario.rain)
        fluid.add_uniform_rain(scenario.depth_inches);
    else
        fluid.add_center_water(scenario.radius, scenario.depth_inches);
}

[[nodiscard]] std::vector<float> capture_water(
    const sim::SimpleCellularFluidSim& fluid,
    int width,
    int depth)
{
    std::vector<float> water;
    water.reserve(static_cast<std::size_t>(width * depth));
    for (int z = 0; z < depth; ++z)
    {
        for (int x = 0; x < width; ++x)
            water.push_back(fluid.water_depth_inches_at(x, z));
    }
    return water;
}

[[nodiscard]] double measure_cpu_baseline_ms_per_step(
    const std::vector<int>& seed,
    int width,
    int depth,
    const Scenario& scenario)
{
    std::vector<double> samples;
    for (int repetition = 0; repetition < k_repetitions; ++repetition)
    {
        sim::SimpleCellularFluidSim fluid;
        configure_baseline(fluid, seed, width, depth, scenario);
        for (int step = 0; step < k_warmup_steps; ++step)
            (void)fluid.step_once();

        const auto start = std::chrono::steady_clock::now();
        for (int step = 0; step < k_measurement_steps; ++step)
            (void)fluid.step_once();
        const auto stop = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(stop - start).count() /
            static_cast<double>(k_measurement_steps));
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

class GpuFluidComputeExperiment
{
public:
    GpuFluidComputeExperiment(
        const std::vector<int>& terrain,
        int width,
        int depth,
        bool tiled)
        : width_(width),
          depth_(depth),
          cell_count_(static_cast<UINT>(terrain.size())),
          tiled_(tiled)
    {
        create_device();
        create_command_objects();
        create_root_signature_and_psos();
        create_descriptor_heap();
        initialize_terrain(terrain);
    }

    ~GpuFluidComputeExperiment()
    {
        if (queue_ && fence_)
        {
            try
            {
                wait_for_gpu();
            }
            catch (...)
            {
            }
        }
        if (fence_event_)
            CloseHandle(fence_event_);
    }

    [[nodiscard]] GpuRunResult run(
        const std::vector<float>& initial_water,
        int warmup_steps,
        int measured_steps)
    {
        create_run_resources();
        record_begin();

        ComPtr<ID3D12Resource> upload = create_buffer(
            static_cast<UINT64>(cell_count_) * sizeof(float),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr;
        D3D12_RANGE no_read{0, 0};
        throw_if_failed(upload->Map(0, &no_read, &mapped), "Map water upload failed.");
        std::memcpy(mapped, initial_water.data(), initial_water.size() * sizeof(float));
        upload->Unmap(0, nullptr);

        command_list_->CopyBufferRegion(
            water_a_.Get(), 0, upload.Get(), 0,
            static_cast<UINT64>(cell_count_) * sizeof(float));
        transition(water_a_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ID3D12DescriptorHeap* heaps[] = { descriptors_.Get() };
        command_list_->SetDescriptorHeaps(1, heaps);
        command_list_->SetComputeRootSignature(root_signature_.Get());

        FluidConstants constants{};
        constants.field_width = static_cast<std::uint32_t>(width_);
        constants.field_depth = static_cast<std::uint32_t>(depth_);
        command_list_->SetComputeRoot32BitConstants(0, 8, &constants, 0);
        command_list_->SetComputeRootDescriptorTable(1, descriptor_gpu(0));
        bool current_is_a = true;

        for (int step = 0; step < warmup_steps; ++step)
            record_step(current_is_a);

        command_list_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        for (int step = 0; step < measured_steps; ++step)
            record_step(current_is_a);
        command_list_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);

        ID3D12Resource* final_water = current_is_a ? water_a_.Get() : water_b_.Get();
        transition(final_water, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list_->CopyBufferRegion(
            water_readback_.Get(), 0, final_water, 0,
            static_cast<UINT64>(cell_count_) * sizeof(float));
        command_list_->ResolveQueryData(
            timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
            timestamp_readback_.Get(), 0);

        throw_if_failed(command_list_->Close(), "Close compute command list failed.");
        ID3D12CommandList* lists[] = { command_list_.Get() };
        const auto submit_start = std::chrono::steady_clock::now();
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();
        const auto submit_stop = std::chrono::steady_clock::now();

        std::uint64_t* timestamps = nullptr;
        D3D12_RANGE timestamp_read{0, sizeof(std::uint64_t) * 2};
        throw_if_failed(
            timestamp_readback_->Map(0, &timestamp_read, reinterpret_cast<void**>(&timestamps)),
            "Map timestamps failed.");
        UINT64 frequency = 0;
        throw_if_failed(queue_->GetTimestampFrequency(&frequency), "GetTimestampFrequency failed.");
        const double kernel_ms =
            static_cast<double>(timestamps[1] - timestamps[0]) /
            static_cast<double>(frequency) * 1000.0;
        timestamp_readback_->Unmap(0, nullptr);

        GpuRunResult result;
        result.kernel_ms_per_step = kernel_ms / static_cast<double>(measured_steps);
        result.submission_ms_per_step =
            std::chrono::duration<double, std::milli>(submit_stop - submit_start).count() /
            static_cast<double>(measured_steps);
        result.water.resize(cell_count_);

        float* result_water = nullptr;
        D3D12_RANGE water_read{
            0, static_cast<SIZE_T>(cell_count_) * sizeof(float)
        };
        throw_if_failed(
            water_readback_->Map(0, &water_read, reinterpret_cast<void**>(&result_water)),
            "Map water readback failed.");
        std::copy(result_water, result_water + cell_count_, result.water.begin());
        water_readback_->Unmap(0, nullptr);
        return result;
    }

private:
    void create_device()
    {
        ComPtr<IDXGIFactory4> factory;
        throw_if_failed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1 failed.");

        for (UINT adapter_index = 0;; ++adapter_index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(adapter_index, &adapter) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(
                    adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_))))
                return;
        }

        throw std::runtime_error("No D3D12 hardware adapter available for compute benchmark.");
    }

    void create_command_objects()
    {
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        throw_if_failed(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)),
            "Create compute command queue failed.");
        throw_if_failed(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator_)),
            "Create compute allocator failed.");
        throw_if_failed(
            device_->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator_.Get(), nullptr,
                IID_PPV_ARGS(&command_list_)),
            "Create compute command list failed.");
        throw_if_failed(command_list_->Close(), "Close initial command list failed.");
        throw_if_failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                        "Create fence failed.");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_)
            throw std::runtime_error("CreateEventW failed.");
    }

    void create_root_signature_and_psos()
    {
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 8;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[4]{};
        for (int i = 0; i < 2; ++i)
        {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = static_cast<UINT>(i);
            ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        for (int i = 2; i < 4; ++i)
        {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = static_cast<UINT>(i - 2);
            ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }

        for (int i = 1; i < 5; ++i)
        {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable.NumDescriptorRanges = 1;
            params[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1];
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 5;
        root_desc.pParameters = params;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        const HRESULT serialize_result = D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
        if (FAILED(serialize_result))
        {
            throw std::runtime_error(errors
                ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                              errors->GetBufferSize())
                : "D3D12SerializeRootSignature failed.");
        }
        throw_if_failed(
            device_->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(),
                IID_PPV_ARGS(&root_signature_)),
            "Create compute root signature failed.");

        const ComPtr<ID3DBlob> propose_shader = load_shader_blob(
            tiled_ ? L"fluid_propose_tiled_cs.cso" : L"fluid_propose_cs.cso");
        const ComPtr<ID3DBlob> apply_shader = load_shader_blob(
            tiled_ ? L"fluid_apply_tiled_cs.cso" : L"fluid_apply_cs.cso");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = root_signature_.Get();
        pso_desc.CS = { propose_shader->GetBufferPointer(), propose_shader->GetBufferSize() };
        throw_if_failed(
            device_->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&propose_pso_)),
            "Create ProposeCS PSO failed.");
        pso_desc.CS = { apply_shader->GetBufferPointer(), apply_shader->GetBufferSize() };
        throw_if_failed(
            device_->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&apply_pso_)),
            "Create ApplyCS PSO failed.");
    }

    void create_descriptor_heap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 6;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(
            device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptors_)),
            "Create compute descriptor heap failed.");
        descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void initialize_terrain(const std::vector<int>& terrain)
    {
        terrain_ = create_buffer(
            static_cast<UINT64>(cell_count_) * sizeof(int),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
        ComPtr<ID3D12Resource> upload = create_buffer(
            static_cast<UINT64>(cell_count_) * sizeof(int),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr;
        D3D12_RANGE no_read{0, 0};
        throw_if_failed(upload->Map(0, &no_read, &mapped), "Map terrain upload failed.");
        std::memcpy(mapped, terrain.data(), terrain.size() * sizeof(int));
        upload->Unmap(0, nullptr);

        record_begin();
        command_list_->CopyBufferRegion(
            terrain_.Get(), 0, upload.Get(), 0,
            static_cast<UINT64>(cell_count_) * sizeof(int));
        transition(terrain_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        execute_recorded_commands();
    }

    void create_run_resources()
    {
        const UINT64 water_size = static_cast<UINT64>(cell_count_) * sizeof(float);
        water_a_ = create_buffer(
            water_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        water_b_ = create_buffer(
            water_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        proposals_ = create_buffer(
            static_cast<UINT64>(cell_count_) * sizeof(FlowProposal),
            D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        water_readback_ = create_buffer(
            water_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);
        timestamp_readback_ = create_buffer(
            sizeof(std::uint64_t) * 2, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_QUERY_HEAP_DESC query_desc{};
        query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query_desc.Count = 2;
        throw_if_failed(
            device_->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&timestamp_heap_)),
            "Create timestamp query heap failed.");

        create_srv(terrain_.Get(), 0, sizeof(int));
        create_srv(water_a_.Get(), 1, sizeof(float));
        create_srv(water_b_.Get(), 2, sizeof(float));
        create_uav(proposals_.Get(), 3, sizeof(FlowProposal));
        create_uav(water_a_.Get(), 4, sizeof(float));
        create_uav(water_b_.Get(), 5, sizeof(float));
    }

    [[nodiscard]] ComPtr<ID3D12Resource> create_buffer(
        UINT64 size,
        D3D12_HEAP_TYPE heap_type,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initial_state)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = heap_type;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = flags;

        ComPtr<ID3D12Resource> resource;
        throw_if_failed(
            device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state,
                nullptr, IID_PPV_ARGS(&resource)),
            "Create compute benchmark buffer failed.");
        return resource;
    }

    void create_srv(ID3D12Resource* resource, UINT slot, UINT stride)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = cell_count_;
        desc.Buffer.StructureByteStride = stride;
        device_->CreateShaderResourceView(resource, &desc, descriptor_cpu(slot));
    }

    void create_uav(ID3D12Resource* resource, UINT slot, UINT stride)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = cell_count_;
        desc.Buffer.StructureByteStride = stride;
        device_->CreateUnorderedAccessView(resource, nullptr, &desc, descriptor_cpu(slot));
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu(UINT slot) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptors_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_size_) * slot;
        return handle;
    }

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE descriptor_gpu(UINT slot) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptors_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(descriptor_size_) * slot;
        return handle;
    }

    void record_begin()
    {
        throw_if_failed(allocator_->Reset(), "Reset compute allocator failed.");
        throw_if_failed(
            command_list_->Reset(allocator_.Get(), nullptr),
            "Reset compute command list failed.");
    }

    void record_step(bool& current_is_a)
    {
        const UINT input_srv_slot = current_is_a ? 1 : 2;
        const UINT output_uav_slot = current_is_a ? 5 : 4;
        ID3D12Resource* input = current_is_a ? water_a_.Get() : water_b_.Get();
        ID3D12Resource* output = current_is_a ? water_b_.Get() : water_a_.Get();
        const UINT groups = (cell_count_ + k_threads_per_group - 1) / k_threads_per_group;

        command_list_->SetComputeRootDescriptorTable(2, descriptor_gpu(input_srv_slot));
        command_list_->SetComputeRootDescriptorTable(3, descriptor_gpu(3));
        command_list_->SetComputeRootDescriptorTable(4, descriptor_gpu(output_uav_slot));
        command_list_->SetPipelineState(propose_pso_.Get());
        if (tiled_)
        {
            command_list_->Dispatch(
                (static_cast<UINT>(width_) + 15u) / 16u,
                (static_cast<UINT>(depth_) + 15u) / 16u,
                1);
        }
        else
        {
            command_list_->Dispatch(groups, 1, 1);
        }

        D3D12_RESOURCE_BARRIER proposal_barrier{};
        proposal_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        proposal_barrier.UAV.pResource = proposals_.Get();
        command_list_->ResourceBarrier(1, &proposal_barrier);

        command_list_->SetPipelineState(apply_pso_.Get());
        command_list_->Dispatch(groups, 1, 1);

        D3D12_RESOURCE_BARRIER uav_barriers[2]{};
        uav_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barriers[0].UAV.pResource = proposals_.Get();
        uav_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barriers[1].UAV.pResource = output;
        command_list_->ResourceBarrier(2, uav_barriers);
        transition(input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        current_is_a = !current_is_a;
    }

    void transition(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &barrier);
    }

    void execute_recorded_commands()
    {
        throw_if_failed(command_list_->Close(), "Close compute initialization list failed.");
        ID3D12CommandList* lists[] = { command_list_.Get() };
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();
    }

    void wait_for_gpu()
    {
        const UINT64 value = next_fence_value_++;
        throw_if_failed(queue_->Signal(fence_.Get(), value), "Signal compute fence failed.");
        if (fence_->GetCompletedValue() < value)
        {
            throw_if_failed(
                fence_->SetEventOnCompletion(value, fence_event_),
                "SetEventOnCompletion failed.");
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    int width_ = 0;
    int depth_ = 0;
    UINT cell_count_ = 0;
    bool tiled_ = false;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 next_fence_value_ = 1;

    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> propose_pso_;
    ComPtr<ID3D12PipelineState> apply_pso_;
    ComPtr<ID3D12DescriptorHeap> descriptors_;
    UINT descriptor_size_ = 0;

    ComPtr<ID3D12Resource> terrain_;
    ComPtr<ID3D12Resource> water_a_;
    ComPtr<ID3D12Resource> water_b_;
    ComPtr<ID3D12Resource> proposals_;
    ComPtr<ID3D12Resource> water_readback_;
    ComPtr<ID3D12QueryHeap> timestamp_heap_;
    ComPtr<ID3D12Resource> timestamp_readback_;
};

[[nodiscard]] bool compare_water_exactly(
    const std::vector<float>& expected,
    const std::vector<float>& actual,
    float& max_abs_difference)
{
    bool exact = true;
    max_abs_difference = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        max_abs_difference = std::max(max_abs_difference, std::abs(expected[i] - actual[i]));
        if (std::bit_cast<std::uint32_t>(expected[i]) !=
            std::bit_cast<std::uint32_t>(actual[i]))
        {
            exact = false;
        }
    }
    return exact;
}

} // namespace

int main()
{
    try
    {
        const std::vector<int> seed = build_live_seed_heights();
        constexpr int detail = sim::GrassField::detail_patch_resolution();
        constexpr int width = 100 * detail;
        constexpr int depth = 100 * detail;
        constexpr Scenario scenarios[] = {
            { "Center pour (r=11, 22 in)", false, 11, 22.0f },
            { "Uniform rain (1 in)", true, 0, 1.0f },
        };
        constexpr int checkpoints[] = { 1, 10, 30 };

        GpuFluidComputeExperiment phase1(seed, width, depth, false);
        GpuFluidComputeExperiment phase2(seed, width, depth, true);
        std::cout << "# grass-field-003 HLSL Compute Benchmark\n\n";
        std::cout << "- Grid: " << width << " x " << depth << " cells\n";
        std::cout << "- GPU timing: timestamp-query kernel time, median of "
                  << k_repetitions << " runs\n";
        std::cout << "- CPU timing: wall-clock time, median of "
                  << k_repetitions << " runs\n\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "| Scenario | CPU baseline ms/step | Phase 1 kernel ms/step | "
                     "Phase 2 tiled kernel ms/step | Phase 1 submit+readback ms/step | "
                     "Phase 2 submit+readback ms/step | Phase 2 vs Phase 1 kernel | "
                     "Exact at 1/10/30 | Max abs difference |\n";
        std::cout << "|---|---:|---:|---:|---:|---:|---:|---|---:|\n";

        bool all_exact = true;
        for (const Scenario& scenario : scenarios)
        {
            sim::SimpleCellularFluidSim initial_state;
            configure_baseline(initial_state, seed, width, depth, scenario);
            const std::vector<float> initial_water =
                capture_water(initial_state, width, depth);
            const double cpu_ms =
                measure_cpu_baseline_ms_per_step(seed, width, depth, scenario);

            std::vector<double> phase1_kernel_samples;
            std::vector<double> phase1_submission_samples;
            std::vector<double> phase2_kernel_samples;
            std::vector<double> phase2_submission_samples;
            for (int repetition = 0; repetition < k_repetitions; ++repetition)
            {
                const GpuRunResult phase1_result =
                    phase1.run(initial_water, k_warmup_steps, k_measurement_steps);
                phase1_kernel_samples.push_back(phase1_result.kernel_ms_per_step);
                phase1_submission_samples.push_back(phase1_result.submission_ms_per_step);

                const GpuRunResult phase2_result =
                    phase2.run(initial_water, k_warmup_steps, k_measurement_steps);
                phase2_kernel_samples.push_back(phase2_result.kernel_ms_per_step);
                phase2_submission_samples.push_back(phase2_result.submission_ms_per_step);
            }
            std::sort(phase1_kernel_samples.begin(), phase1_kernel_samples.end());
            std::sort(phase1_submission_samples.begin(), phase1_submission_samples.end());
            std::sort(phase2_kernel_samples.begin(), phase2_kernel_samples.end());
            std::sort(phase2_submission_samples.begin(), phase2_submission_samples.end());
            const double phase1_kernel_ms =
                phase1_kernel_samples[phase1_kernel_samples.size() / 2];
            const double phase1_submission_ms =
                phase1_submission_samples[phase1_submission_samples.size() / 2];
            const double phase2_kernel_ms =
                phase2_kernel_samples[phase2_kernel_samples.size() / 2];
            const double phase2_submission_ms =
                phase2_submission_samples[phase2_submission_samples.size() / 2];

            bool scenario_exact = true;
            float max_abs_difference = 0.0f;
            for (int checkpoint : checkpoints)
            {
                sim::SimpleCellularFluidSim cpu;
                configure_baseline(cpu, seed, width, depth, scenario);
                for (int step = 0; step < checkpoint; ++step)
                    (void)cpu.step_once();
                const std::vector<float> expected = capture_water(cpu, width, depth);
                const GpuRunResult phase1_actual = phase1.run(initial_water, 0, checkpoint);
                const GpuRunResult phase2_actual = phase2.run(initial_water, 0, checkpoint);
                float phase1_difference = 0.0f;
                float phase2_difference = 0.0f;
                scenario_exact = compare_water_exactly(
                    expected, phase1_actual.water, phase1_difference) && scenario_exact;
                scenario_exact = compare_water_exactly(
                    expected, phase2_actual.water, phase2_difference) && scenario_exact;
                max_abs_difference = std::max({
                    max_abs_difference,
                    phase1_difference,
                    phase2_difference
                });
            }
            all_exact = all_exact && scenario_exact;

            std::cout << "| " << scenario.name
                      << " | " << cpu_ms
                      << " | " << phase1_kernel_ms
                      << " | " << phase2_kernel_ms
                      << " | " << phase1_submission_ms
                      << " | " << phase2_submission_ms
                      << " | " << phase1_kernel_ms / phase2_kernel_ms << "x"
                      << " | " << (scenario_exact ? "PASS" : "FAIL")
                      << " | " << max_abs_difference << " |\n";
        }

        return all_exact ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "GPU benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
