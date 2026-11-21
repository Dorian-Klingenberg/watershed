#include "native_d3d12_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace windows_d3d12_fluid
{

void InitializationReport::reset()
{
    observations_.clear();
}

void InitializationReport::record(
    InitializationStage stage,
    std::int32_t hresult,
    std::int32_t device_removed_reason,
    std::uint64_t elapsed_ms)
{
    observations_.push_back({
        stage,
        hresult,
        device_removed_reason,
        elapsed_ms
    });
}

const std::vector<InitializationObservation>& InitializationReport::observations() const noexcept
{
    return observations_;
}

bool InitializationReport::has_failure() const noexcept
{
    for (const InitializationObservation& observation : observations_)
    {
        if (observation.hresult < 0)
            return true;
    }
    return false;
}

std::string InitializationReport::text() const
{
    std::ostringstream output;
    output << "D3D12 initialization report:\n";
    for (const InitializationObservation& observation : observations_)
    {
        output << "  "
            << initialization_stage_name(observation.stage)
            << " hr=0x"
            << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
            << static_cast<std::uint32_t>(observation.hresult)
            << " removed=0x"
            << std::setw(8) << static_cast<std::uint32_t>(observation.device_removed_reason)
            << " elapsed_ms=" << std::dec << observation.elapsed_ms
            << '\n';
    }
    return output.str();
}

const char* initialization_stage_name(InitializationStage stage) noexcept
{
    switch (stage)
    {
    case InitializationStage::constructor: return "constructor";
    case InitializationStage::com_init: return "com_init";
    case InitializationStage::dxgi_factory: return "dxgi_factory";
    case InitializationStage::d3d12_device: return "d3d12_device";
    case InitializationStage::command_queue: return "command_queue";
    case InitializationStage::fence: return "fence";
    case InitializationStage::composition_swap_chain: return "composition_swap_chain";
    case InitializationStage::swap_chain_create: return "swap_chain_create";
    case InitializationStage::rtv_heap_create: return "rtv_heap_create";
    case InitializationStage::backbuffers_create: return "backbuffers_create";
    case InitializationStage::field_resources: return "field_resources";
    case InitializationStage::field_texture_create: return "field_texture_create";
    case InitializationStage::field_upload_create: return "field_upload_create";
    case InitializationStage::srv_heap_create: return "srv_heap_create";
    case InitializationStage::srv_create: return "srv_create";
    case InitializationStage::pipeline: return "pipeline";
    case InitializationStage::reset: return "reset";
    case InitializationStage::reset_start: return "reset_start";
    case InitializationStage::reset_texture_upload: return "reset_texture_upload";
    case InitializationStage::reset_command_record: return "reset_command_record";
    case InitializationStage::reset_texture_copy: return "reset_texture_copy";
    case InitializationStage::reset_resource_barriers: return "reset_resource_barriers";
    case InitializationStage::reset_draw: return "reset_draw";
    case InitializationStage::reset_present: return "reset_present";
    case InitializationStage::root_signature_serialize: return "root_signature_serialize";
    case InitializationStage::root_signature_create: return "root_signature_create";
    case InitializationStage::shaders: return "shaders";
    case InitializationStage::graphics_pso: return "graphics_pso";
    }
    return "unknown";
}

bool is_redundant_resource_transition(
    std::uint32_t before,
    std::uint32_t after) noexcept
{
    return before == after;
}

bool is_valid_tick_batch(std::uint32_t ticks) noexcept
{
    return ticks == 1 || ticks == 2 || ticks == 4 || ticks == 8;
}

std::uint32_t normalized_field_coordinate(
    float normalized,
    float panNormalized,
    float zoom,
    std::uint32_t extent) noexcept
{
    if (!std::isfinite(normalized) || !std::isfinite(panNormalized) ||
        !std::isfinite(zoom) || zoom < 1.0f || extent == 0)
        return 0;

    const float clampedNormalized = std::clamp(normalized, 0.0f, 1.0f);
    const float clampedPan = std::clamp(panNormalized, 0.0f, 1.0f);
    const float fieldCoordinate =
        (clampedNormalized - 0.5f) * zoom + clampedPan;
    const float clampedCoordinate = std::clamp(fieldCoordinate, 0.0f, 1.0f);
    return static_cast<std::uint32_t>(
        std::clamp(std::round(clampedCoordinate * extent),
            0.0f,
            static_cast<float>(extent - 1)));
}

float normalized_view_yaw(float yawDegrees) noexcept
{
    if (!std::isfinite(yawDegrees))
        return 0.0f;
    float yaw = std::fmod(yawDegrees, 360.0f);
    if (yaw < 0.0f)
        yaw += 360.0f;
    return yaw;
}

float clamped_view_elevation(float elevationDegrees) noexcept
{
    if (!std::isfinite(elevationDegrees))
        return 45.0f;
    return std::clamp(elevationDegrees, 14.323945f, 80.0f);
}

}
