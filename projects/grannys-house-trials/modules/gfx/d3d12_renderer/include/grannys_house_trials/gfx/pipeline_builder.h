#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <wrl/client.h>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

using Microsoft::WRL::ComPtr;

/// Loads compiled shader blobs from disk and caches pipeline state objects.
///
/// Design principles:
/// - Shaders are pre-compiled offline (.cso files); no runtime compilation
/// - Missing shaders are detected at load time and throw immediately
/// - Pipeline state objects are cached by name for reuse
///
/// RAII behavior:
/// - Constructor: creates empty cache
/// - load_compiled_shader(): loads .cso file and returns ID3DBlob
/// - build_pipeline(): creates PSO and adds to cache
/// - Destructor: releases all cached PSOs and loaded blobs
///
/// Exception safety:
/// - Throws std::runtime_error if shader file is missing or D3D12 fails
/// - All cached PSOs are automatically released on destruction
/// - Move semantics supported, copy forbidden
///
/// Typical usage:
/// @code
///     PipelineBuilder builder;
///     auto vs = builder.load_compiled_shader("shaders/terrain.vs.cso");
///     auto ps = builder.load_compiled_shader("shaders/terrain.ps.cso");
///
///     D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = { /* ... */ };
///     desc.pRootSignature = root_sig.Get();
///     desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
///     desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
///
///     auto pso = builder.build_pipeline(device, "terrain_coarse", desc);
///     // ... render using pso ...
///     auto cached_pso = builder.get_cached_pipeline("terrain_coarse"); // retrieves from cache
/// @endcode
class PipelineBuilder {
public:
    /// Constructor: creates empty pipeline cache.
    PipelineBuilder() = default;

    /// Destructor: releases all cached PSOs and loaded shaders.
    /// No-throw guarantee.
    ~PipelineBuilder() = default;

    // Move semantics
    PipelineBuilder(PipelineBuilder&&) = default;
    PipelineBuilder& operator=(PipelineBuilder&&) = default;

    // Copy forbidden
    PipelineBuilder(const PipelineBuilder&) = delete;
    PipelineBuilder& operator=(const PipelineBuilder&) = delete;

    // ========== Shader Loading ==========

    /// Loads a pre-compiled shader from disk.
    /// Shader files are expected to be in the executable directory under "shaders/".
    /// @param relative_path Path relative to executable directory (e.g., "terrain.vs.cso")
    /// @returns ID3DBlob containing shader bytecode
    /// @throws std::runtime_error if file not found or read fails
    ComPtr<ID3DBlob> load_compiled_shader(std::string_view relative_path);

    // ========== Pipeline Creation ==========

    /// Creates a root signature from descriptor.
    /// Validates descriptor and throws on failure.
    /// @param device Valid ID3D12Device pointer
    /// @param desc D3D12_ROOT_SIGNATURE_DESC to compile
    /// @returns Created root signature
    /// @throws std::runtime_error on serialization or creation failure
    ComPtr<ID3D12RootSignature> create_root_signature(
        ID3D12Device* device,
        const D3D12_ROOT_SIGNATURE_DESC& desc);

    /// Creates and caches a graphics pipeline state object.
    /// Validates PSO descriptor and checks root signature.
    /// Pipeline is cached by name for later retrieval via get_cached_pipeline().
    /// @param device Valid ID3D12Device pointer
    /// @param debug_name Unique name for caching (used in diagnostics)
    /// @param desc Fully-populated D3D12_GRAPHICS_PIPELINE_STATE_DESC
    ///        (must include valid root signature, shaders, blend state, etc.)
    /// @returns Created pipeline state object
    /// @throws std::runtime_error if PSO creation fails
    /// @throws std::invalid_argument if debug_name already exists in cache
    ComPtr<ID3D12PipelineState> build_pipeline(
        ID3D12Device* device,
        std::string_view debug_name,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

    // ========== Cache Access ==========

    /// Retrieves a previously-created pipeline from cache.
    /// @param name Debug name used in build_pipeline()
    /// @returns Cached PSO, or nullptr if not found
    /// No-throw, const-qualified.
    ID3D12PipelineState* get_cached_pipeline(std::string_view name) const noexcept;

    /// Clears all cached pipelines.
    /// Useful for hot-reloading or cleanup.
    /// No-throw.
    void clear_cache() noexcept;

private:
    std::map<std::string, ComPtr<ID3D12PipelineState>> pso_cache_;
};

} // namespace grannys_house_trials::gfx
