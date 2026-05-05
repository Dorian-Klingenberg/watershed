#include "grannys_house_trials/gfx/pipeline_builder.h"

#include <array>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

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

std::filesystem::path get_executable_directory() {
    std::array<wchar_t, MAX_PATH> module_path{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        module_path.data(),
        static_cast<DWORD>(module_path.size()));

    if (length == 0 || length >= module_path.size()) {
        throw std::runtime_error("Failed to get executable path");
    }

    return std::filesystem::path(module_path.data()).parent_path();
}

} // anonymous namespace

// ============================================================================
// PipelineBuilder Implementation
// ============================================================================

ComPtr<ID3DBlob> PipelineBuilder::load_compiled_shader(std::string_view relative_path)
{
    std::filesystem::path exe_dir = get_executable_directory();
    std::filesystem::path full_path = exe_dir / std::filesystem::path(relative_path.begin(), relative_path.end());

    // Check if file exists
    if (!std::filesystem::exists(full_path)) {
        std::ostringstream ss;
        ss << "Shader file not found: " << full_path.string();
        throw std::runtime_error(ss.str());
    }

    // Read shader blob from file
    ComPtr<ID3DBlob> shader_blob;
    throw_if_failed(
        D3DReadFileToBlob(full_path.c_str(), &shader_blob),
        "Failed to read shader file");

    return shader_blob;
}

ComPtr<ID3D12RootSignature> PipelineBuilder::create_root_signature(
    ID3D12Device* device,
    const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    if (!device) {
        throw std::invalid_argument("Device pointer cannot be null");
    }

    // Serialize root signature
    ComPtr<ID3DBlob> signature_blob;
    ComPtr<ID3DBlob> error_blob;

    HRESULT hr = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature_blob,
        &error_blob);

    if (FAILED(hr)) {
        if (error_blob) {
            std::string error_msg(
                static_cast<const char*>(error_blob->GetBufferPointer()),
                error_blob->GetBufferSize());
            throw std::runtime_error("Failed to serialize root signature: " + error_msg);
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    // Create root signature
    ComPtr<ID3D12RootSignature> root_signature;
    throw_if_failed(
        device->CreateRootSignature(
            0,
            signature_blob->GetBufferPointer(),
            signature_blob->GetBufferSize(),
            IID_PPV_ARGS(&root_signature)),
        "Failed to create root signature");

    return root_signature;
}

ComPtr<ID3D12PipelineState> PipelineBuilder::build_pipeline(
    ID3D12Device* device,
    std::string_view debug_name,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
    if (!device) {
        throw std::invalid_argument("Device pointer cannot be null");
    }

    if (debug_name.empty()) {
        throw std::invalid_argument("Debug name cannot be empty");
    }

    // Check if already cached
    std::string name_str(debug_name);
    auto it = pso_cache_.find(name_str);
    if (it != pso_cache_.end()) {
        throw std::invalid_argument("Pipeline state already exists with this name");
    }

    // Create pipeline state
    ComPtr<ID3D12PipelineState> pso;
    throw_if_failed(
        device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)),
        "Failed to create graphics pipeline state");

    // Cache the PSO
    pso_cache_[name_str] = pso;

    return pso;
}

ID3D12PipelineState* PipelineBuilder::get_cached_pipeline(std::string_view name) const noexcept
{
    std::string name_str(name);
    auto it = pso_cache_.find(name_str);
    if (it != pso_cache_.end()) {
        return it->second.Get();
    }
    return nullptr;
}

void PipelineBuilder::clear_cache() noexcept
{
    pso_cache_.clear();
}

} // namespace grannys_house_trials::gfx
