#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace grannys_house_trials::gfx {

using Microsoft::WRL::ComPtr;

/// Manages Direct3D12 device lifetime, swap chain, command queue, and frame synchronization.
///
/// This class is the root of all GPU resource ownership. It follows RAII semantics:
/// - Constructor creates and validates all D3D12 infrastructure or throws
/// - Destructor performs explicit GPU wait and automatic ComPtr cleanup
/// - Copy construction is forbidden (prevents double-cleanup)
/// - Move construction is allowed (transfers ownership)
///
/// Exception Safety:
/// - Constructor: strong guarantee (all-or-nothing initialization)
/// - Other methods: basic guarantee (may leave some state inconsistent, but no leaks)
/// - Destructor: no-throw (waits GPU, then cleans up)
///
/// Windows assumptions:
/// - HWND must be valid at construction time
/// - HWND must remain valid for all rendering calls
/// - Caller is responsible for handling WM_SIZE (future extension: resize support)
///
/// Design: All COM objects managed via ComPtr<>, ensuring cleanup even if exceptions occur.
class D3D12Context {
public:
    /// Constructor: creates device, factory, command queue, swap chain, and frame synchronization.
    ///
    /// Throws std::runtime_error if any D3D12 operation fails.
    /// On exception, all partially-initialized ComPtr<> objects are automatically destroyed.
    ///
    /// @param hwnd Valid window handle for swap chain binding
    /// @param width Backbuffer width in pixels (must be > 0)
    /// @param height Backbuffer height in pixels (must be > 0)
    /// @param frame_count Number of frames in flight (typically 2)
    /// @throws std::runtime_error on DXGI or D3D12 failure
    explicit D3D12Context(
        HWND hwnd,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t frame_count = 2);

    /// Destructor: waits for GPU to finish all queued work, then releases all resources.
    /// No-throw guarantee (does not throw exceptions in destructor).
    ~D3D12Context();

    // RAII: prevent accidental copies (would cause double-cleanup)
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;

    // Move semantics allowed (transfers ownership)
    D3D12Context(D3D12Context&&) = default;
    D3D12Context& operator=(D3D12Context&&) = default;

    // ========== Device Access ==========

    /// Returns the underlying ID3D12Device pointer.
    /// The returned pointer is valid for the lifetime of this context.
    /// No-throw, const-qualified.
    ID3D12Device* device() const noexcept;

    /// Returns the underlying ID3D12CommandQueue pointer.
    /// The returned command queue is used by GraphicsFrame objects for frame execution.
    /// No-throw, const-qualified.
    ID3D12CommandQueue* command_queue() const noexcept;

    /// Returns the graphics command list bound to the current frame.
    /// The command list is allocated and reset at frame boundaries.
    /// No-throw, const-qualified.
    ID3D12GraphicsCommandList* command_list() const noexcept;

    /// Returns the command allocator for the given frame index.
    /// Command allocators are pooled per-frame and are reset at frame start.
    /// @param frame_idx Index into [0, frame_count)
    /// @throws std::out_of_range if frame_idx >= frame_count
    ID3D12CommandAllocator* command_allocator(std::uint32_t frame_idx) const;

    // ========== Frame State ==========

    /// Returns the current frame index (advances on each present() call).
    /// Useful for selecting which per-frame resources to use.
    /// Returns: value in [0, frame_count)
    /// No-throw, const-qualified.
    std::uint32_t current_frame_index() const noexcept;

    /// Returns the total number of frames presented since construction.
    /// Useful for frame-relative timing and animation.
    /// No-throw, const-qualified.
    std::uint64_t total_frames_presented() const noexcept;

    /// Returns the current render target resource for the current frame.
    /// The render target is suitable for use as a D3D12_RESOURCE_STATE_RENDER_TARGET.
    /// No-throw, const-qualified.
    ID3D12Resource* current_render_target() const noexcept;

    /// Returns the CPU descriptor handle for the current frame's render target view.
    /// Use with command_list()->OMSetRenderTargets().
    /// No-throw, const-qualified.
    D3D12_CPU_DESCRIPTOR_HANDLE current_rtv_handle() const noexcept;

    // ========== Resource Queries ==========

    /// Returns the size of an RTV (render target view) descriptor in bytes.
    /// Useful for manual descriptor heap offset calculations (rarely needed).
    /// No-throw, const-qualified.
    std::uint32_t rtv_descriptor_size() const noexcept;

    // ========== Frame Lifecycle ==========

    /// Blocks CPU until all GPU work is complete.
    /// Call this before destroying the context or before major synchronization points.
    /// @throws std::runtime_error if fence wait fails
    void wait_for_gpu();

    /// Presents the current backbuffer and advances to the next frame.
    /// Implicitly calls wait_for_gpu() to ensure frame synchronization.
    /// After present(), current_frame_index() returns the next frame's index.
    /// @param sync_interval Vertical sync: 0 = immediate, 1 = vsync, etc.
    /// @throws std::runtime_error if present fails
    void present(std::uint32_t sync_interval = 1);

    /// Returns whether swapchain resize is currently supported by this context.
    /// For now this returns false; callers should avoid resize attempts.
    bool supports_resize() const noexcept;

    /// Resize API boundary.
    /// Currently not supported and will throw std::logic_error.
    ///
    /// @param width Requested backbuffer width
    /// @param height Requested backbuffer height
    /// @throws std::logic_error always (until implemented)
    void resize(std::uint32_t width, std::uint32_t height);

private:
    ComPtr<IDXGIFactory7> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<IDXGISwapChain4> swap_chain_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;

    std::uint32_t rtv_descriptor_size_ = 0;
    std::uint32_t frame_count_ = 2;
    std::uint32_t current_frame_index_ = 0;
    std::uint64_t total_frames_presented_ = 0;
    std::uint64_t fence_value_ = 1;

    // Per-frame resources
    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> command_list;
        ComPtr<ID3D12Resource> render_target;
        std::uint64_t fence_value = 0;
    };

    std::vector<FrameResources> frames_;

    // Helper functions (implementation)
    void move_to_next_frame_impl();
};

} // namespace grannys_house_trials::gfx
