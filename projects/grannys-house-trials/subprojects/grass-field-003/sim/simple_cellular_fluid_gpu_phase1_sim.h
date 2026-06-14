#pragma once

// SimpleCellularFluidGpuPhase1Sim -- interactive adapter for preserved HLSL
// compute experiments. Phase 1 reads each result back for existing renderers;
// Phase 2 uses a tiled proposal shader and can keep water GPU-resident for a
// matching renderer, with snapshots read back only for inspection or editing.

#include "i_field_sim.h"

#include "../gfx/shader_utils.h"
#include "../third_party/imgui/imgui.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace grannys_house_trials::sim
{

class SimpleCellularFluidGpuPhase1Sim final : public IFieldSim
{
    using ComPtr = Microsoft::WRL::ComPtr<ID3D12Resource>;

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

    struct FlowProposal
    {
        float total_outflow = 0.0f;
        float send_left = 0.0f;
        float send_right = 0.0f;
        float send_up = 0.0f;
        float send_down = 0.0f;
    };

    struct WaterSummary
    {
        int wet_cell_count = 0;
        float total_water_inches = 0.0f;
        float max_water_depth_inches = 0.0f;
    };

    static_assert(sizeof(FluidConstants) == 32);
    static_assert(sizeof(FlowProposal) == 20);
    static constexpr UINT k_threads_per_group = 256;

public:
    explicit SimpleCellularFluidGpuPhase1Sim(
        ID3D12Device* device,
        bool tiled_gpu_resident = false)
        : tiled_propose_(tiled_gpu_resident),
          gpu_resident_rendering_(tiled_gpu_resident),
          display_name_(tiled_gpu_resident
              ? "Cellular Water Flow (HLSL Phase 2 - Tiled GPU Resident)"
              : "Cellular Water Flow (HLSL Compute Phase 1)")
    {
        if (!device)
            throw std::invalid_argument("SimpleCellularFluidGpuPhase1Sim: device is null.");

        device_ = device;
        create_command_objects();
        create_root_signature_and_psos();
        create_descriptor_heap();
    }

    ~SimpleCellularFluidGpuPhase1Sim() override
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

    [[nodiscard]] const char* name() const noexcept override
    {
        return display_name_.c_str();
    }

    [[nodiscard]] int width() const noexcept override { return width_; }
    [[nodiscard]] int depth() const noexcept override { return depth_; }

    void set_selected_cell(int x, int z, bool valid) noexcept override
    {
        selected_x_ = x;
        selected_z_ = z;
        selected_cell_valid_ =
            valid && x >= 0 && x < width_ && z >= 0 && z < depth_;
    }

    [[nodiscard]] int height_at(int x, int z) const override
    {
        return static_cast<int>(std::lround(surface_height_inches_at(x, z)));
    }

    [[nodiscard]] int water_depth_at(int x, int z) const override
    {
        return static_cast<int>(std::lround(water_depth_inches_at(x, z)));
    }

    [[nodiscard]] float surface_height_inches_at(int x, int z) const override
    {
        const std::size_t i = index_of(x, z);
        return static_cast<float>(terrain_heights_[i]) + water_depths_[i];
    }

    [[nodiscard]] float water_depth_inches_at(int x, int z) const override
    {
        return water_depths_[index_of(x, z)];
    }

    [[nodiscard]] bool has_gpu_resident_field() const noexcept override
    {
        return gpu_resident_rendering_;
    }

    [[nodiscard]] ID3D12Resource* gpu_terrain_resource() const noexcept override
    {
        return gpu_resident_rendering_ ? terrain_.Get() : nullptr;
    }

    [[nodiscard]] ID3D12Resource* gpu_water_resource() const noexcept override
    {
        if (!gpu_resident_rendering_)
            return nullptr;
        return current_is_a_ ? water_a_.Get() : water_b_.Get();
    }

    void reset(int new_width, int new_depth, std::vector<int> heights_inches) override
    {
        if (new_width <= 0 || new_depth <= 0)
            throw std::invalid_argument("SimpleCellularFluidGpuPhase1Sim: width and depth must be positive.");

        if (static_cast<int>(heights_inches.size()) != new_width * new_depth)
            throw std::invalid_argument("SimpleCellularFluidGpuPhase1Sim: seed size does not match dimensions.");

        width_ = new_width;
        depth_ = new_depth;
        cell_count_ = static_cast<UINT>(heights_inches.size());
        terrain_heights_ = std::move(heights_inches);
        water_depths_.assign(terrain_heights_.size(), 0.0f);
        cycle_count_ = 0;
        last_step_ms_ = 0.0;
        total_step_ms_ = 0.0;
        summary_dirty_ = true;
        cpu_snapshot_current_ = true;

        create_grid_resources();
        upload_reset_state();
    }

    void add_center_water(int radius, float depth_inches)
    {
        ensure_cpu_snapshot_for_edit();
        add_water_disc(width_ / 2, depth_ / 2, radius, depth_inches);
    }

    void add_uniform_rain(float depth_inches)
    {
        ensure_cpu_snapshot_for_edit();
        for (float& water : water_depths_)
            water += depth_inches;
        mark_cpu_water_changed();
    }

    [[nodiscard]] bool step_once() override
    {
        if (water_depths_.empty())
            return false;

        const auto start = std::chrono::steady_clock::now();
        if (gpu_water_needs_upload_)
            upload_cpu_water();

        record_begin();

        ID3D12Resource* input = current_is_a_ ? water_a_.Get() : water_b_.Get();
        ID3D12Resource* output = current_is_a_ ? water_b_.Get() : water_a_.Get();
        D3D12_RESOURCE_STATES& input_state = current_is_a_ ? water_a_state_ : water_b_state_;
        D3D12_RESOURCE_STATES& output_state = current_is_a_ ? water_b_state_ : water_a_state_;
        const UINT input_srv_slot = current_is_a_ ? 1 : 2;
        const UINT output_uav_slot = current_is_a_ ? 5 : 4;

        transition(input, input_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(output, output_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12DescriptorHeap* heaps[] = { descriptors_.Get() };
        command_list_->SetDescriptorHeaps(1, heaps);
        command_list_->SetComputeRootSignature(root_signature_.Get());

        FluidConstants constants{};
        constants.field_width = static_cast<std::uint32_t>(width_);
        constants.field_depth = static_cast<std::uint32_t>(depth_);
        constants.max_flow_inches = max_flow_inches_;
        constants.settle_rate = settle_rate_;
        constants.drain_edges = drain_edges_ ? 1u : 0u;
        command_list_->SetComputeRoot32BitConstants(0, 8, &constants, 0);
        command_list_->SetComputeRootDescriptorTable(1, descriptor_gpu(0));
        command_list_->SetComputeRootDescriptorTable(2, descriptor_gpu(input_srv_slot));
        command_list_->SetComputeRootDescriptorTable(3, descriptor_gpu(3));
        command_list_->SetComputeRootDescriptorTable(4, descriptor_gpu(output_uav_slot));

        command_list_->SetPipelineState(propose_pso_.Get());
        if (tiled_propose_)
        {
            command_list_->Dispatch(
                (static_cast<UINT>(width_) + 15u) / 16u,
                (static_cast<UINT>(depth_) + 15u) / 16u,
                1);
        }
        else
        {
            const UINT groups = (cell_count_ + k_threads_per_group - 1) / k_threads_per_group;
            command_list_->Dispatch(groups, 1, 1);
        }
        uav_barrier(proposals_.Get());

        command_list_->SetPipelineState(apply_pso_.Get());
        const UINT groups = (cell_count_ + k_threads_per_group - 1) / k_threads_per_group;
        command_list_->Dispatch(groups, 1, 1);
        uav_barrier(proposals_.Get());
        uav_barrier(output);

        if (gpu_resident_rendering_)
        {
            transition(input, input_state, D3D12_RESOURCE_STATE_COMMON);
            transition(output, output_state, D3D12_RESOURCE_STATE_COMMON);
            execute_recorded_commands();
            cpu_snapshot_current_ = false;
        }
        else
        {
            transition(output, output_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            command_list_->CopyBufferRegion(water_readback_.Get(), 0, output, 0, water_buffer_size());
            execute_recorded_commands();
            read_back_water();
            cpu_snapshot_current_ = true;
            summary_dirty_ = true;
        }
        current_is_a_ = !current_is_a_;
        ++cycle_count_;

        const auto stop = std::chrono::steady_clock::now();
        last_step_ms_ = std::chrono::duration<double, std::milli>(stop - start).count();
        total_step_ms_ += last_step_ms_;
        return true;
    }

    [[nodiscard]] bool render_ui() override
    {
        ensure_summary_current();

        ImGui::Text("Cycles: %d", cycle_count_);
        ImGui::Text("Wet cells: %d", summary_.wet_cell_count);
        ImGui::Text("Total water: %.1f in", summary_.total_water_inches);
        ImGui::Text("Max depth: %.2f in", summary_.max_water_depth_inches);
        ImGui::Text("Last interactive tick: %.3f ms", last_step_ms_);
        if (cycle_count_ > 0)
            ImGui::Text("Mean interactive tick: %.3f ms", total_step_ms_ / static_cast<double>(cycle_count_));
        ImGui::TextDisabled(gpu_resident_rendering_
            ? "Tiled HLSL; renderer reads GPU water without per-tick readback."
            : "HLSL compute plus readback for the existing renderer.");
        if (gpu_resident_rendering_ && !cpu_snapshot_current_)
            ImGui::TextDisabled("Statistics and selected-cell readout show the last snapshot.");

        ImGui::Separator();

        ImGui::SliderInt("Brush radius", &add_radius_, 1, 16);
        ImGui::SliderInt("Add depth", &add_depth_inches_, 1, 48, "%d in");
        ImGui::SliderFloat("Max flow", &max_flow_inches_, 0.1f, 8.0f, "%.2f in/step");
        ImGui::SliderFloat("Settle rate", &settle_rate_, 0.05f, 1.0f, "%.2f");
        ImGui::Checkbox("Drain edges", &drain_edges_);

        bool changed = false;
        if (selected_cell_valid_)
            ImGui::TextDisabled("Water brush target: selected [%d, %d]", selected_x_, selected_z_);
        else
            ImGui::TextDisabled("Water brush target: field center.");

        if (ImGui::Button("Add Water at Target"))
        {
            ensure_cpu_snapshot_for_edit();
            const auto center = water_brush_center();
            add_water_disc(center.first, center.second, add_radius_,
                           static_cast<float>(add_depth_inches_));
            changed = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Add Rain"))
        {
            add_uniform_rain(1.0f);
            changed = true;
        }

        if (ImGui::Button("Step (x1)"))
            changed = step_once() || changed;

        ImGui::SameLine();
        if (ImGui::Button("Step (x50)"))
        {
            for (int step = 0; step < 50; ++step)
                changed = step_once() || changed;
        }

        ImGui::SameLine();
        if (ImGui::Button("Evaporate 1 in"))
        {
            ensure_cpu_snapshot_for_edit();
            for (float& water : water_depths_)
                water = std::max(0.0f, water - 1.0f);
            mark_cpu_water_changed();
            changed = true;
        }

        if (changed)
            ensure_summary_current();

        ImGui::Separator();
        if (gpu_resident_rendering_ && ImGui::Button("Read Back Snapshot"))
        {
            read_back_current_state();
            changed = true;
        }
        ImGui::TextDisabled("Use a benchmark target for kernel-only timing.");
        ImGui::TextDisabled(gpu_resident_rendering_
            ? "Renderer height = GPU terrain + GPU resident water."
            : "Renderer height = terrain + read-back water surface.");
        return changed;
    }

private:
    [[nodiscard]] std::pair<int, int> water_brush_center() const noexcept
    {
        if (selected_cell_valid_)
            return { selected_x_, selected_z_ };

        return { width_ / 2, depth_ / 2 };
    }

    [[nodiscard]] std::size_t index_of(int x, int z) const
    {
        if (x < 0 || x >= width_ || z < 0 || z >= depth_)
            throw std::out_of_range("SimpleCellularFluidGpuPhase1Sim: coordinates out of range.");
        return static_cast<std::size_t>(z * width_ + x);
    }

    [[nodiscard]] UINT64 water_buffer_size() const
    {
        return static_cast<UINT64>(cell_count_) * sizeof(float);
    }

    static void check(HRESULT result, const char* message)
    {
        if (FAILED(result))
            throw std::runtime_error(message);
    }

    void create_command_objects()
    {
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        check(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)),
              "Create interactive fluid compute queue failed.");
        check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator_)),
              "Create interactive fluid compute allocator failed.");
        check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator_.Get(), nullptr,
                                         IID_PPV_ARGS(&command_list_)),
              "Create interactive fluid compute command list failed.");
        check(command_list_->Close(), "Close initial interactive fluid command list failed.");
        check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
              "Create interactive fluid fence failed.");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_)
            throw std::runtime_error("Create interactive fluid fence event failed.");
    }

    void create_root_signature_and_psos()
    {
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 8;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE ranges[4]{};
        for (int range = 0; range < 2; ++range)
        {
            ranges[range].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[range].NumDescriptors = 1;
            ranges[range].BaseShaderRegister = static_cast<UINT>(range);
            ranges[range].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        for (int range = 2; range < 4; ++range)
        {
            ranges[range].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[range].NumDescriptors = 1;
            ranges[range].BaseShaderRegister = static_cast<UINT>(range - 2);
            ranges[range].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        for (int parameter = 1; parameter < 5; ++parameter)
        {
            params[parameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[parameter].DescriptorTable.NumDescriptorRanges = 1;
            params[parameter].DescriptorTable.pDescriptorRanges = &ranges[parameter - 1];
            params[parameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 5;
        root_desc.pParameters = params;
        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT serialized = D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
        if (FAILED(serialized))
        {
            throw std::runtime_error(errors
                ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                : "Serialize interactive fluid root signature failed.");
        }
        check(device_->CreateRootSignature(
                  0, signature->GetBufferPointer(), signature->GetBufferSize(),
                  IID_PPV_ARGS(&root_signature_)),
              "Create interactive fluid root signature failed.");

        const auto propose_shader = gfx::load_shader_blob(
            gfx::to_utf8(gfx::exe_dir() + (tiled_propose_
                ? L"shaders\\fluid_propose_tiled_cs.cso"
                : L"shaders\\fluid_propose_cs.cso")));
        const auto apply_shader = gfx::load_shader_blob(
            gfx::to_utf8(gfx::exe_dir() + (tiled_propose_
                ? L"shaders\\fluid_apply_tiled_cs.cso"
                : L"shaders\\fluid_apply_cs.cso")));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = root_signature_.Get();
        pso_desc.CS = { propose_shader.data(), propose_shader.size() };
        check(device_->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&propose_pso_)),
              "Create interactive fluid ProposeCS pipeline failed.");
        pso_desc.CS = { apply_shader.data(), apply_shader.size() };
        check(device_->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&apply_pso_)),
              "Create interactive fluid ApplyCS pipeline failed.");
    }

    void create_descriptor_heap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 6;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptors_)),
              "Create interactive fluid descriptors failed.");
        descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    [[nodiscard]] ComPtr create_buffer(
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

        ComPtr resource;
        check(device_->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr,
                  IID_PPV_ARGS(&resource)),
              "Create interactive fluid buffer failed.");
        return resource;
    }

    void create_grid_resources()
    {
        const UINT64 terrain_size = static_cast<UINT64>(cell_count_) * sizeof(int);
        terrain_ = create_buffer(terrain_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
        terrain_upload_ = create_buffer(terrain_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_GENERIC_READ);
        water_a_ = create_buffer(water_buffer_size(), D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
        water_b_ = create_buffer(water_buffer_size(), D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        water_upload_ = create_buffer(water_buffer_size(), D3D12_HEAP_TYPE_UPLOAD,
                                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        water_readback_ = create_buffer(water_buffer_size(), D3D12_HEAP_TYPE_READBACK,
                                        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        proposals_ = create_buffer(static_cast<UINT64>(cell_count_) * sizeof(FlowProposal),
                                   D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        terrain_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
        water_a_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
        water_b_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        current_is_a_ = true;

        create_srv(terrain_.Get(), 0, sizeof(int));
        create_srv(water_a_.Get(), 1, sizeof(float));
        create_srv(water_b_.Get(), 2, sizeof(float));
        create_uav(proposals_.Get(), 3, sizeof(FlowProposal));
        create_uav(water_a_.Get(), 4, sizeof(float));
        create_uav(water_b_.Get(), 5, sizeof(float));
    }

    void upload_reset_state()
    {
        write_upload(terrain_upload_.Get(), terrain_heights_.data(),
                     terrain_heights_.size() * sizeof(int));
        write_upload(water_upload_.Get(), water_depths_.data(),
                     water_depths_.size() * sizeof(float));

        record_begin();
        command_list_->CopyBufferRegion(
            terrain_.Get(), 0, terrain_upload_.Get(), 0,
            static_cast<UINT64>(cell_count_) * sizeof(int));
        command_list_->CopyBufferRegion(water_a_.Get(), 0, water_upload_.Get(), 0, water_buffer_size());
        transition(terrain_.Get(), terrain_state_, read_state_after_upload());
        transition(water_a_.Get(), water_a_state_, read_state_after_upload());
        execute_recorded_commands();
        gpu_water_needs_upload_ = false;
    }

    void upload_cpu_water()
    {
        write_upload(water_upload_.Get(), water_depths_.data(),
                     water_depths_.size() * sizeof(float));
        record_begin();
        transition(water_a_.Get(), water_a_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(water_b_.Get(), water_b_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        command_list_->CopyBufferRegion(water_a_.Get(), 0, water_upload_.Get(), 0, water_buffer_size());
        transition(water_a_.Get(), water_a_state_, read_state_after_upload());
        execute_recorded_commands();
        current_is_a_ = true;
        gpu_water_needs_upload_ = false;
    }

    void write_upload(ID3D12Resource* upload, const void* source, std::size_t bytes)
    {
        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        check(upload->Map(0, &no_read, &mapped), "Map interactive fluid upload failed.");
        std::memcpy(mapped, source, bytes);
        upload->Unmap(0, nullptr);
    }

    void read_back_water()
    {
        float* mapped = nullptr;
        const D3D12_RANGE read{0, static_cast<SIZE_T>(water_buffer_size())};
        check(water_readback_->Map(0, &read, reinterpret_cast<void**>(&mapped)),
              "Map interactive fluid readback failed.");
        std::copy(mapped, mapped + water_depths_.size(), water_depths_.begin());
        water_readback_->Unmap(0, nullptr);
    }

    void read_back_current_state()
    {
        ID3D12Resource* current = current_is_a_ ? water_a_.Get() : water_b_.Get();
        D3D12_RESOURCE_STATES& current_state =
            current_is_a_ ? water_a_state_ : water_b_state_;
        record_begin();
        transition(current, current_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list_->CopyBufferRegion(
            water_readback_.Get(), 0, current, 0, water_buffer_size());
        transition(current, current_state, read_state_after_upload());
        execute_recorded_commands();
        read_back_water();
        cpu_snapshot_current_ = true;
        summary_dirty_ = true;
    }

    void ensure_cpu_snapshot_for_edit()
    {
        if (gpu_resident_rendering_ && !cpu_snapshot_current_)
            read_back_current_state();
    }

    [[nodiscard]] D3D12_RESOURCE_STATES read_state_after_upload() const noexcept
    {
        return gpu_resident_rendering_
            ? D3D12_RESOURCE_STATE_COMMON
            : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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
        check(allocator_->Reset(), "Reset interactive fluid allocator failed.");
        check(command_list_->Reset(allocator_.Get(), nullptr),
              "Reset interactive fluid command list failed.");
    }

    void execute_recorded_commands()
    {
        check(command_list_->Close(), "Close interactive fluid command list failed.");
        ID3D12CommandList* lists[] = { command_list_.Get() };
        queue_->ExecuteCommandLists(1, lists);
        wait_for_gpu();
    }

    void wait_for_gpu()
    {
        const UINT64 value = next_fence_value_++;
        check(queue_->Signal(fence_.Get(), value), "Signal interactive fluid fence failed.");
        if (fence_->GetCompletedValue() < value)
        {
            check(fence_->SetEventOnCompletion(value, fence_event_),
                  "Wait for interactive fluid fence failed.");
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& before,
                    D3D12_RESOURCE_STATES after)
    {
        if (before == after)
            return;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &barrier);
        before = after;
    }

    void uav_barrier(ID3D12Resource* resource)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        command_list_->ResourceBarrier(1, &barrier);
    }

    void add_water_disc(int center_x, int center_z, int radius, float depth_inches)
    {
        const int radius_squared = radius * radius;
        for (int z = std::max(0, center_z - radius); z <= std::min(depth_ - 1, center_z + radius); ++z)
        {
            for (int x = std::max(0, center_x - radius); x <= std::min(width_ - 1, center_x + radius); ++x)
            {
                const int dx = x - center_x;
                const int dz = z - center_z;
                if (dx * dx + dz * dz <= radius_squared)
                    water_depths_[index_of(x, z)] += depth_inches;
            }
        }
        mark_cpu_water_changed();
    }

    void mark_cpu_water_changed()
    {
        gpu_water_needs_upload_ = true;
        summary_dirty_ = true;
    }

    void ensure_summary_current()
    {
        if (!summary_dirty_)
            return;

        summary_ = {};
        for (float water : water_depths_)
        {
            if (water > 0.001f)
                ++summary_.wet_cell_count;
            summary_.total_water_inches += water;
            summary_.max_water_depth_inches = std::max(summary_.max_water_depth_inches, water);
        }
        summary_dirty_ = false;
    }

    int width_ = 0;
    int depth_ = 0;
    UINT cell_count_ = 0;
    int cycle_count_ = 0;
    int selected_x_ = 0;
    int selected_z_ = 0;
    int add_radius_ = 11;
    int add_depth_inches_ = 22;
    float max_flow_inches_ = 2.0f;
    float settle_rate_ = 0.5f;
    bool drain_edges_ = true;
    bool current_is_a_ = true;
    bool gpu_water_needs_upload_ = false;
    bool summary_dirty_ = true;
    bool tiled_propose_ = false;
    bool gpu_resident_rendering_ = false;
    bool cpu_snapshot_current_ = true;
    bool selected_cell_valid_ = false;
    double last_step_ms_ = 0.0;
    double total_step_ms_ = 0.0;
    WaterSummary summary_{};
    std::vector<int> terrain_heights_;
    std::vector<float> water_depths_;
    std::string display_name_;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 next_fence_value_ = 1;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> propose_pso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> apply_pso_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors_;
    UINT descriptor_size_ = 0;

    ComPtr terrain_;
    ComPtr terrain_upload_;
    ComPtr water_a_;
    ComPtr water_b_;
    ComPtr water_upload_;
    ComPtr water_readback_;
    ComPtr proposals_;
    D3D12_RESOURCE_STATES terrain_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES water_a_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES water_b_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

} // namespace grannys_house_trials::sim
