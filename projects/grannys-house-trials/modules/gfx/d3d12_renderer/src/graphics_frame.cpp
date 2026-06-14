#include "grannys_house_trials/gfx/graphics_frame.h"
#include "grannys_house_trials/gfx/d3d12_context.h"

#include <stdexcept>
#include <windows.h>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

namespace {

void throw_if_failed(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

void debug_log(const std::string& message) {
    std::string line = message + "\n";
    OutputDebugStringA(line.c_str());
}

} // anonymous namespace

// ============================================================================
// GraphicsFrame Implementation
// ============================================================================

GraphicsFrame::GraphicsFrame(D3D12Context* context, std::uint32_t frame_index)
    : context_(context), frame_index_(frame_index), is_open_(false), is_closed_(false)
{
    if (!context) {
        throw std::invalid_argument("Context pointer cannot be null");
    }
}

GraphicsFrame::~GraphicsFrame() {
    if (is_open_ && !is_closed_) {
        debug_log("GraphicsFrame destroyed without calling end()");
    }
}

void GraphicsFrame::begin() {
    if (is_open_) {
        throw std::logic_error("GraphicsFrame::begin() called while frame is already open");
    }

    if (is_closed_) {
        throw std::logic_error("GraphicsFrame::begin() called after frame has been closed");
    }

    if (frame_index_ != context_->current_frame_index()) {
        throw std::logic_error("GraphicsFrame frame index does not match current context frame");
    }

    // Validate that the context has valid per-frame objects
    ID3D12CommandAllocator* allocator = context_->command_allocator(frame_index_);
    ID3D12GraphicsCommandList* cmd_list = context_->command_list();
    if (!allocator || !cmd_list) {
        throw std::logic_error("Context has no valid command list");
    }

    // Open command recording for this frame.
    throw_if_failed(
        allocator->Reset(),
        "Failed to reset command allocator");

    throw_if_failed(
        cmd_list->Reset(allocator, nullptr),
        "Failed to reset command list");

    is_open_ = true;
}

void GraphicsFrame::end() {
    if (!is_open_) {
        throw std::logic_error("GraphicsFrame::end() called without matching begin()");
    }

    if (is_closed_) {
        throw std::logic_error("GraphicsFrame::end() called multiple times");
    }

    // Close the command list
    ID3D12GraphicsCommandList* cmd_list = command_list();
    if (cmd_list) {
        throw_if_failed(
            cmd_list->Close(),
            "Failed to close command list");
    }

    is_closed_ = true;
}

void GraphicsFrame::execute() {
    if (!is_closed_) {
        throw std::logic_error("GraphicsFrame::execute() called before end()");
    }

    // Submit command list to command queue
    ID3D12GraphicsCommandList* cmd_list = command_list();
    if (!cmd_list || !context_) {
        throw std::logic_error("Frame state corrupted");
    }

    ID3D12CommandList* cmd_lists[] = {cmd_list};
    context_->command_queue()->ExecuteCommandLists(std::size(cmd_lists), cmd_lists);
}

void GraphicsFrame::transition_to_render_target() {
    if (!is_open_ || is_closed_) {
        throw std::logic_error("Frame is not in recording state");
    }

    ID3D12Resource* render_target = context_->current_render_target();
    if (!render_target) {
        throw std::logic_error("No valid render target");
    }

    transition_resource(
        render_target,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void GraphicsFrame::transition_to_present() {
    if (!is_open_ || is_closed_) {
        throw std::logic_error("Frame is not in recording state");
    }

    ID3D12Resource* render_target = context_->current_render_target();
    if (!render_target) {
        throw std::logic_error("No valid render target");
    }

    transition_resource(
        render_target,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
}

void GraphicsFrame::transition_resource(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    if (!is_open_ || is_closed_) {
        throw std::logic_error("Frame is not in recording state");
    }

    if (!resource) {
        // No-op if resource is null
        return;
    }

    if (before == after) {
        // No-op if states are the same
        return;
    }

    ID3D12GraphicsCommandList* cmd_list = command_list();
    if (!cmd_list) {
        throw std::logic_error("No command list available");
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmd_list->ResourceBarrier(1, &barrier);
}

ID3D12GraphicsCommandList* GraphicsFrame::command_list() const noexcept {
    if (!context_) {
        return nullptr;
    }
    // Return the command list for the current frame context
    return context_->command_list();
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsFrame::render_target_view() const noexcept {
    if (context_) {
        return context_->current_rtv_handle();
    }
    return D3D12_CPU_DESCRIPTOR_HANDLE{};
}

} // namespace grannys_house_trials::gfx
