#pragma once

#include <functional>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

class D3D12Context;

// Small helper that owns the common frame boilerplate for UI-only passes.
class UIFrameRenderer {
public:
    using RecordCallback = std::function<void(ID3D12GraphicsCommandList*)>;

    void render(
        D3D12Context& context,
        ID3D12DescriptorHeap* shader_visible_heap,
        const float clear_color[4],
        const RecordCallback& record_callback) const;
};

} // namespace grannys_house_trials::gfx
