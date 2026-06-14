#pragma once

#include <cstdint>
#include <DirectXMath.h>

namespace grannys_house_trials::gfx {

/// Standard uniform buffer layout for scene rendering.
/// Maps directly to HLSL cbuffer in shaders.
struct SceneConstants {
    DirectX::XMFLOAT4X4 inverse_view_projection;
    DirectX::XMFLOAT4 camera_world_position;
    DirectX::XMFLOAT4 field_origin_and_voxel_size;
    DirectX::XMUINT4 field_info;
    DirectX::XMUINT4 selection_info;
    DirectX::XMUINT4 refinement_info;
    DirectX::XMUINT4 display_info;
};

/// Per-frame metadata for debugging and temporal effects.
struct FrameMetadata {
    std::uint32_t frame_index;
    std::uint64_t frame_count;
    float delta_time;
    float elapsed_time;
};

} // namespace grannys_house_trials::gfx
