#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace windows_d3d12_fluid
{

enum class InitializationStage : std::uint32_t
{
    constructor = 1,
    com_init = 2,
    dxgi_factory = 21,
    d3d12_device = 22,
    command_queue = 23,
    fence = 24,
    composition_swap_chain = 4,
    swap_chain_create = 31,
    rtv_heap_create = 35,
    backbuffers_create = 36,
    field_resources = 5,
    field_texture_create = 41,
    field_upload_create = 42,
    srv_heap_create = 43,
    srv_create = 44,
    pipeline = 6,
    reset = 7,
    reset_start = 71,
    reset_texture_upload = 72,
    reset_command_record = 73,
    reset_texture_copy = 74,
    reset_resource_barriers = 75,
    reset_draw = 76,
    reset_present = 77,
    root_signature_serialize = 511,
    root_signature_create = 512,
    shaders = 52,
    graphics_pso = 53
};

struct InitializationObservation
{
    InitializationStage stage = InitializationStage::constructor;
    std::int32_t hresult = 0;
    std::int32_t device_removed_reason = 0;
    std::uint64_t elapsed_ms = 0;
};

class InitializationReport
{
public:
    void reset();
    void record(
        InitializationStage stage,
        std::int32_t hresult,
        std::int32_t device_removed_reason,
        std::uint64_t elapsed_ms);

    [[nodiscard]] const std::vector<InitializationObservation>& observations() const noexcept;
    [[nodiscard]] std::string text() const;
    [[nodiscard]] bool has_failure() const noexcept;

private:
    std::vector<InitializationObservation> observations_;
};

[[nodiscard]] const char* initialization_stage_name(InitializationStage stage) noexcept;
[[nodiscard]] bool is_redundant_resource_transition(
    std::uint32_t before,
    std::uint32_t after) noexcept;

}
