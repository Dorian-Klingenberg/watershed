#include "grannys_house_trials/gfx/d3d12_context.h"

#include <stdexcept>
#include <sstream>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

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
// D3D12Context Implementation
// ============================================================================

D3D12Context::D3D12Context(
    HWND hwnd,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t frame_count)
    : frame_count_(frame_count)
{
    if (!hwnd || width == 0 || height == 0) {
        throw std::invalid_argument("Invalid HWND or dimensions");
    }

    try {
        // Create DXGI factory
        throw_if_failed(
            CreateDXGIFactory1(IID_PPV_ARGS(&factory_)),
            "Failed to create DXGI factory");

        // Create D3D12 device with feature level 11.0
        throw_if_failed(
            D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
            "Failed to create D3D12 device");

        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        throw_if_failed(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
            "Failed to create command queue");

        // Create swap chain
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.BufferCount = frame_count_;
        swap_desc.Width = width;
        swap_desc.Height = height;
        swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.SampleDesc.Quality = 0;

        ComPtr<IDXGISwapChain1> initial_swap_chain;
        throw_if_failed(
            factory_->CreateSwapChainForHwnd(
                command_queue_.Get(),
                hwnd,
                &swap_desc,
                nullptr,
                nullptr,
                &initial_swap_chain),
            "Failed to create swap chain");

        throw_if_failed(
            initial_swap_chain.As(&swap_chain_),
            "Failed to upgrade swap chain interface to IDXGISwapChain4");

        current_frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

        // Create RTV descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.NumDescriptors = frame_count_;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        throw_if_failed(
            device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)),
            "Failed to create RTV descriptor heap");

        rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create per-frame resources
        frames_.resize(frame_count_);
        for (std::uint32_t i = 0; i < frame_count_; ++i) {
            // Create command allocator
            throw_if_failed(
                device_->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&frames_[i].allocator)),
                "Failed to create command allocator");

            // Get render target from swap chain
            throw_if_failed(
                swap_chain_->GetBuffer(i, IID_PPV_ARGS(&frames_[i].render_target)),
                "Failed to get swap chain buffer");

            // Create render target view
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
            rtv_handle.ptr += static_cast<SIZE_T>(i) * rtv_descriptor_size_;
            device_->CreateRenderTargetView(frames_[i].render_target.Get(), nullptr, rtv_handle);

            frames_[i].fence_value = 0;
        }

        // Create command list (for frame 0, will be reset per-frame)
        throw_if_failed(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                frames_[0].allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&frames_[0].command_list)),
            "Failed to create command list");

        // Command list must start closed
        throw_if_failed(
            frames_[0].command_list->Close(),
            "Failed to close initial command list");

        // Create fence for synchronization
        throw_if_failed(
            device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
            "Failed to create fence");

        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_) {
            throw std::runtime_error("Failed to create fence event");
        }

    } catch (...) {
        // All ComPtr<> are automatically cleaned up by destructors
        // Explicit cleanup not needed, but close fence event if it was created
        if (fence_event_) {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        throw;
    }
}

D3D12Context::~D3D12Context() {
    try {
        // Wait for GPU to complete all queued work before cleanup
        if (device_ && fence_) {
            wait_for_gpu();
        }
    } catch (...) {
        // Suppress exceptions in destructor
        debug_log("Exception during GPU wait in D3D12Context destructor");
    }

    // Close fence event handle
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }

    // ComPtr<> objects automatically clean up their COM references
    frames_.clear();
}

ID3D12Device* D3D12Context::device() const noexcept {
    return device_.Get();
}

ID3D12CommandQueue* D3D12Context::command_queue() const noexcept {
    return command_queue_.Get();
}

ID3D12GraphicsCommandList* D3D12Context::command_list() const noexcept {
    if (current_frame_index_ < frames_.size()) {
        return frames_[current_frame_index_].command_list.Get();
    }
    return nullptr;
}

ID3D12CommandAllocator* D3D12Context::command_allocator(std::uint32_t frame_idx) const {
    if (frame_idx >= frame_count_) {
        throw std::out_of_range("Frame index out of range");
    }
    return frames_[frame_idx].allocator.Get();
}

std::uint32_t D3D12Context::current_frame_index() const noexcept {
    return current_frame_index_;
}

std::uint64_t D3D12Context::total_frames_presented() const noexcept {
    return total_frames_presented_;
}

ID3D12Resource* D3D12Context::current_render_target() const noexcept {
    if (current_frame_index_ < frames_.size()) {
        return frames_[current_frame_index_].render_target.Get();
    }
    return nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::current_rtv_handle() const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(current_frame_index_) * rtv_descriptor_size_;
    return handle;
}

std::uint32_t D3D12Context::rtv_descriptor_size() const noexcept {
    return rtv_descriptor_size_;
}

void D3D12Context::wait_for_gpu() {
    if (!fence_ || !fence_event_) {
        throw std::runtime_error("Fence not initialized");
    }

    // Signal the fence with a new value
    const std::uint64_t signal_value = fence_value_;
    throw_if_failed(
        command_queue_->Signal(fence_.Get(), signal_value),
        "Failed to signal fence");

    // Wait for the fence to reach the signal value
    if (fence_->GetCompletedValue() < signal_value) {
        throw_if_failed(
            fence_->SetEventOnCompletion(signal_value, fence_event_),
            "Failed to set fence event");
        WaitForSingleObjectEx(fence_event_, INFINITE, FALSE);
    }

    fence_value_++;
}

void D3D12Context::present(std::uint32_t sync_interval) {
    // Wait for GPU to complete rendering
    wait_for_gpu();

    // Present the back buffer
    throw_if_failed(
        swap_chain_->Present(sync_interval, 0),
        "Failed to present swap chain");

    // Record fence value for this frame
    frames_[current_frame_index_].fence_value = fence_value_;

    // Move to next frame
    move_to_next_frame_impl();
}

bool D3D12Context::supports_resize() const noexcept {
    return false;
}

void D3D12Context::resize(std::uint32_t width, std::uint32_t height) {
    (void)width;
    (void)height;
    throw std::logic_error("D3D12Context::resize() is not implemented yet");
}

void D3D12Context::move_to_next_frame_impl() {
    // Get the next frame index
    current_frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    total_frames_presented_++;

    // Reset the command allocator for this frame
    // Wait if GPU is still using it from a previous cycle
    if (frames_[current_frame_index_].fence_value != 0 &&
        fence_->GetCompletedValue() < frames_[current_frame_index_].fence_value) {
        throw_if_failed(
            fence_->SetEventOnCompletion(frames_[current_frame_index_].fence_value, fence_event_),
            "Failed to set fence completion event");
        WaitForSingleObjectEx(fence_event_, INFINITE, FALSE);
    }

    throw_if_failed(
        frames_[current_frame_index_].allocator->Reset(),
        "Failed to reset command allocator");

    // Recreate command list if needed
    if (!frames_[current_frame_index_].command_list) {
        throw_if_failed(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                frames_[current_frame_index_].allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&frames_[current_frame_index_].command_list)),
            "Failed to create command list for next frame");

        // Command list is created open; close it so frame begin() can Reset it.
        throw_if_failed(
            frames_[current_frame_index_].command_list->Close(),
            "Failed to close newly created command list");
    }
}

} // namespace grannys_house_trials::gfx
