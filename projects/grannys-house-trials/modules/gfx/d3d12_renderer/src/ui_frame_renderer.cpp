#include "grannys_house_trials/gfx/ui_frame_renderer.h"

#include "grannys_house_trials/gfx/d3d12_context.h"
#include "grannys_house_trials/gfx/graphics_frame.h"

#include <stdexcept>

namespace grannys_house_trials::gfx {

void UIFrameRenderer::render(
    D3D12Context& context,
    ID3D12DescriptorHeap* shader_visible_heap,
    const float clear_color[4],
    const RecordCallback& record_callback) const
{
    if (!clear_color) {
        throw std::invalid_argument("clear_color cannot be null");
    }

    GraphicsFrame frame(&context, context.current_frame_index());
    frame.begin();
    frame.transition_to_render_target();

    ID3D12GraphicsCommandList* command_list = frame.command_list();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = context.current_rtv_handle();

    command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

    if (shader_visible_heap) {
        ID3D12DescriptorHeap* descriptor_heaps[] = {shader_visible_heap};
        command_list->SetDescriptorHeaps(1, descriptor_heaps);
    }

    if (record_callback) {
        record_callback(command_list);
    }

    frame.transition_to_present();
    frame.end();
    frame.execute();
    context.present(1);
}

} // namespace grannys_house_trials::gfx
