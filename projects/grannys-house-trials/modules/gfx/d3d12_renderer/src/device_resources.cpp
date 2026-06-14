#include "grannys_house_trials/gfx/device_resources.h"

#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <windows.h>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

namespace {

void throw_if_failed(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

ComPtr<ID3D12Resource> create_upload_buffer(
    ID3D12Device* device,
    std::uint64_t size,
    const char* error_message)
{
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ComPtr<ID3D12Resource> buffer;
    throw_if_failed(
        device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)),
        error_message);

    return buffer;
}

ComPtr<ID3D12Resource> create_gpu_default_buffer(
    ID3D12Device* device,
    std::uint64_t size,
    const char* error_message)
{
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> buffer;
    throw_if_failed(
        device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&buffer)),
        error_message);

    return buffer;
}

} // anonymous namespace

// ============================================================================
// GPUBuffer<T> Template Implementation
// ============================================================================

template<typename T>
GPUBuffer<T>::GPUBuffer(ID3D12Device* device, std::uint32_t initial_capacity)
    : device_(device), element_count_(0), capacity_(0), is_dirty_(false)
{
    if (!device) {
        throw std::invalid_argument("Device pointer cannot be null");
    }

    if (initial_capacity > 0) {
        allocate_impl(initial_capacity);
    }
}

template<typename T>
void GPUBuffer<T>::update(const T* data, std::uint32_t count, std::uint32_t offset)
{
    if (!data || count == 0) {
        return;
    }

    if (offset + count > element_count_) {
        throw std::out_of_range("Update range exceeds buffer bounds");
    }

    if (!staging_data_) {
        throw std::logic_error("Staging buffer not initialized");
    }

    // Copy data to staging buffer
    std::memcpy(
        staging_data_ + offset,
        data,
        count * sizeof(T));

    is_dirty_ = true;
}

template<typename T>
void GPUBuffer<T>::resize(std::uint32_t new_capacity)
{
    if (new_capacity <= capacity_) {
        // No reallocation needed
        return;
    }

    allocate_impl(new_capacity);
    element_count_ = (std::min)(element_count_, new_capacity);
}

template<typename T>
void GPUBuffer<T>::upload(ID3D12GraphicsCommandList* command_list)
{
    if (!command_list || !is_dirty_) {
        return;
    }

    if (!gpu_buffer_ || !staging_buffer_) {
        throw std::logic_error("GPU or staging buffer not initialized");
    }

    // Record copy command
    const std::uint64_t copy_size = static_cast<std::uint64_t>(element_count_) * sizeof(T);
    command_list->CopyBufferRegion(
        gpu_buffer_.Get(),
        0,
        staging_buffer_.Get(),
        0,
        copy_size);

    // Record transition to allow GPU to read
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = gpu_buffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    command_list->ResourceBarrier(1, &barrier);

    is_dirty_ = false;
}

template<typename T>
void GPUBuffer<T>::clear()
{
    element_count_ = 0;
    is_dirty_ = false;
    if (staging_data_) {
        std::memset(staging_data_, 0, capacity_ * sizeof(T));
    }
}

template<typename T>
ID3D12Resource* GPUBuffer<T>::gpu_resource() const noexcept
{
    return gpu_buffer_.Get();
}

template<typename T>
std::uint32_t GPUBuffer<T>::element_count() const noexcept
{
    return element_count_;
}

template<typename T>
std::uint32_t GPUBuffer<T>::capacity() const noexcept
{
    return capacity_;
}

template<typename T>
bool GPUBuffer<T>::is_dirty() const noexcept
{
    return is_dirty_;
}

template<typename T>
void GPUBuffer<T>::allocate_impl(std::uint32_t new_capacity)
{
    if (new_capacity == 0) {
        throw std::invalid_argument("Capacity must be > 0");
    }

    if (!device_) {
        throw std::logic_error("Device not initialized");
    }

    // Allocate GPU buffer (DEFAULT heap, optimal for GPU access)
    const std::uint64_t gpu_size = static_cast<std::uint64_t>(new_capacity) * sizeof(T);
    gpu_buffer_ = create_gpu_default_buffer(
        device_,
        gpu_size,
        "Failed to create GPU buffer");

    // Allocate staging buffer (UPLOAD heap, optimal for CPU writes)
    const std::uint64_t staging_size = gpu_size;
    staging_buffer_ = create_upload_buffer(
        device_,
        staging_size,
        "Failed to create staging buffer");

    // Map staging buffer for CPU access
    D3D12_RANGE read_range{0, 0}; // We will only write
    throw_if_failed(
        staging_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&staging_data_)),
        "Failed to map staging buffer");

    capacity_ = new_capacity;
    element_count_ = new_capacity;
    is_dirty_ = true;
}

// ============================================================================
// Explicit Template Instantiations
// ============================================================================

// Instantiate for common element types used in rendering
template class GPUBuffer<std::uint32_t>;
template class GPUBuffer<std::int32_t>;
template class GPUBuffer<float>;
template class GPUBuffer<std::byte>;

// ============================================================================
// DeviceResources Implementation
// ============================================================================

DeviceResources::DeviceResources(ID3D12Device* device)
    : device_(device)
{
    if (!device) {
        throw std::invalid_argument("Device pointer cannot be null");
    }
}

} // namespace grannys_house_trials::gfx
