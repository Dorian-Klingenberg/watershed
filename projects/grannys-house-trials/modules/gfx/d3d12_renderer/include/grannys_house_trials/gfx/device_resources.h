#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

using Microsoft::WRL::ComPtr;

/// Template for typed GPU buffers with CPU-side staging and automatic upload synchronization.
///
/// Each GPUBuffer<T> manages:
/// - GPU-side buffer (device-local, read-only from GPU perspective)
/// - CPU-visible staging buffer for updates
/// - Capacity and dirty-flag tracking
///
/// RAII behavior:
/// - Constructor allocates initial GPU and staging buffers
/// - update() writes to staging buffer and marks dirty
/// - upload() records buffer copy command to command list
/// - Destructor releases all resources automatically
///
/// Exception safety:
/// - Constructor: strong guarantee (allocation succeeds or throws)
/// - resize(): may throw std::bad_alloc on out-of-memory
/// - update(): basic guarantee (partial data may be updated)
/// - upload(): basic guarantee (copy may partially execute)
///
/// Move semantics:
/// - Move construction transfers ownership of GPU and staging buffers
/// - Copy construction is forbidden (prevents double-cleanup)
///
/// Threading:
/// - update() is CPU-side only (not GPU-dependent)
/// - upload() must be called from a GraphicsFrame context (GPU-command-dependent)
/// - No synchronization primitives; caller responsible for frame synchronization
///
/// Typical usage:
/// @code
///     GPUBuffer<TerrainCell> terrain(device, 10000);
///     // ... populate data ...
///     terrain.update(cells.data(), cells.size(), 0);
///     // ... in render frame ...
///     {
///         GraphicsFrame frame(&context, frame_index);
///         frame.begin();
///         terrain.upload(frame.command_list());
///         // ... bind terrain.gpu_resource() to pipeline ...
///         frame.end();
///     }
/// @endcode
template<typename T>
class GPUBuffer {
public:
    /// Constructor: allocates GPU and staging buffers.
    /// @param device Valid ID3D12Device pointer
    /// @param initial_capacity Initial element count (may be 0)
    /// @throws std::bad_alloc if allocation fails
    explicit GPUBuffer(ID3D12Device* device, std::uint32_t initial_capacity);

    /// Destructor: releases GPU and staging buffers automatically.
    /// No-throw guarantee.
    ~GPUBuffer() = default;

    // Move semantics (transfer ownership)
    GPUBuffer(GPUBuffer&&) = default;
    GPUBuffer& operator=(GPUBuffer&&) = default;

    // Copy semantics forbidden (prevent double-cleanup)
    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;

    // ========== Data Management ==========

    /// Updates the staging buffer with new element data.
    /// Does NOT copy to GPU (call upload() to do that).
    /// Data is staged for next upload() call.
    /// @param data Pointer to element data (must be valid for count elements)
    /// @param count Number of elements to update
    /// @param offset Element offset in buffer (default 0 = start)
    /// @throws std::out_of_range if offset + count > element_count()
    void update(const T* data, std::uint32_t count, std::uint32_t offset = 0);

    /// Resizes the buffer to hold new_capacity elements.
    /// If new_capacity <= current capacity, this is a no-op.
    /// If new_capacity > capacity, reallocates GPU and staging buffers.
    /// @param new_capacity New desired capacity in elements
    /// @throws std::bad_alloc if reallocation fails
    void resize(std::uint32_t new_capacity);

    /// Records GPU memory copy command to command list.
    /// Copies staged data to GPU-side buffer.
    /// Must be called from a GraphicsFrame context (during command recording).
    /// @param command_list Valid ID3D12GraphicsCommandList pointer
    void upload(ID3D12GraphicsCommandList* command_list);

    /// Clears all buffer data and resets dirty flag.
    /// Does not deallocate GPU/staging memory.
    void clear();

    // ========== Access & Queries ==========

    /// Returns the GPU-side buffer resource.
    /// Suitable for binding as SRV, UAV, or resource reference.
    /// No-throw, const-qualified.
    ID3D12Resource* gpu_resource() const noexcept;

    /// Returns current element count (not capacity).
    /// Elements [0, element_count()) contain valid data.
    /// No-throw, const-qualified.
    std::uint32_t element_count() const noexcept;

    /// Returns allocated capacity in elements.
    /// capacity() >= element_count() always.
    /// No-throw, const-qualified.
    std::uint32_t capacity() const noexcept;

    /// Returns true if buffer has pending updates not yet uploaded.
    /// No-throw, const-qualified.
    bool is_dirty() const noexcept;

private:
    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12Resource> gpu_buffer_;
    ComPtr<ID3D12Resource> staging_buffer_;
    T* staging_data_ = nullptr;

    std::uint32_t element_count_ = 0;
    std::uint32_t capacity_ = 0;
    bool is_dirty_ = false;

    // Helper
    void allocate_impl(std::uint32_t new_capacity);
};

/// High-level GPU resource management for multiple buffer types.
/// Currently a placeholder for future descriptor heap and resource pooling.
class DeviceResources {
public:
    /// Constructor.
    explicit DeviceResources(ID3D12Device* device);

    // RAII
    ~DeviceResources() = default;
    DeviceResources(DeviceResources&&) = default;
    DeviceResources& operator=(DeviceResources&&) = default;
    DeviceResources(const DeviceResources&) = delete;
    DeviceResources& operator=(const DeviceResources&) = delete;

private:
    ID3D12Device* device_ = nullptr;
};

} // namespace grannys_house_trials::gfx
