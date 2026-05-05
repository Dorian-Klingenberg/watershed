#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <uxtheme.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

// As required by ImGui docs - WndProcHandler is intentionally behind #if 0 in the header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/gfx/d3d12_context.h"
#include "grannys_house_trials/gfx/ui_frame_renderer.h"
#include "grannys_house_trials/gfx/orbit_camera.h"
#include "grannys_house_trials/playtest/grannys_yard_session.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"
#include "grannys_house_trials/sim/sparse_refined_patch_field.h"

using namespace grannys_house_trials;

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
using namespace DirectX;

constexpr UINT frame_count = 2;
constexpr float pi = 3.1415926535f;
constexpr int default_field_size = 100;
constexpr float default_voxel_size_feet = 1.0f;
constexpr wchar_t main_window_class_name[] = L"GrannysHouseTrialsGrassField002";

// Forward declarations for Win32 window procedure
struct Application;
Application* g_app = nullptr;

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    // Route Win32 message to ImGui
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
        return true;

    if (g_app == nullptr)
        return DefWindowProcW(hwnd, message, wparam, lparam);

    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_MOUSEMOVE:
    case WM_RBUTTONDOWN:
        break;

    case WM_SIZE:
    {
        // Handle window resize
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

struct VoxelInfo
{
    int x, y, z;
    float elevation;
    float moisture;
    std::string ground_type;
    bool is_soaking;
    std::string description;
};

struct CameraMatrices
{
    XMMATRIX view = XMMatrixIdentity();
    XMMATRIX projection = XMMatrixIdentity();
    XMFLOAT3 eye_position{};
    XMFLOAT3 target_position{};
};

struct Ray
{
    XMFLOAT3 origin{};
    XMFLOAT3 direction{};
};

struct MouseState
{
    enum class DragMode
    {
        none,
        pan,
        orbit,
    };

    POINT anchor_position{};
    POINT last_position{};
    bool left_button_down = false;
    bool right_button_down = false;
    bool dragging = false;
    DragMode drag_mode = DragMode::none;
};

struct VoxelSelection
{
    int x = 0;
    int y = 0;
    int z = 0;
};

struct SceneConstants
{
    XMFLOAT4X4 inverse_view_projection;
    XMFLOAT4 camera_world_position{};
    XMFLOAT4 field_origin_and_voxel_size{};
    XMUINT4 field_info{};
    XMUINT4 selection_info{};
    XMUINT4 refinement_info{};
    XMUINT4 display_info{};
};

struct GpuFieldCell
{
    float column_height_voxels = 0.0f;
    std::uint32_t material = 0;
    std::uint32_t is_homestead_pad = 0;
    std::uint32_t is_garden_bed = 0;
    float soil_moisture = 0.0f;
    float fertility = 0.0f;
    float sunlight = 0.0f;
    float weed_pressure = 0.0f;
    float coarse_full_height_voxels = 0.0f;
};

struct GpuRefinedPatchMetadata
{
    std::uint32_t height_offset = 0;
    std::uint32_t material = 0;
    std::uint32_t is_homestead_pad = 0;
    std::uint32_t is_garden_bed = 0;
    float soil_moisture = 0.0f;
    float fertility = 0.0f;
    float sunlight = 0.0f;
    float weed_pressure = 0.0f;
    float coarse_full_height_voxels = 0.0f;
    float patch_max_height_voxels = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct SparseRefinedPatchGpuData
{
    std::vector<std::int32_t> lookup;
    std::vector<GpuRefinedPatchMetadata> metadata;
    std::vector<std::uint32_t> heights;
};

void throw_if_failed(HRESULT result, const char* message)
{
    if (FAILED(result))
    {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool is_in_rect(int x, int z, int min_x, int max_x, int min_z, int max_z) noexcept
{
    return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
}

[[nodiscard]] std::optional<sim::TargetId> scenario_target_for_cell(int x, int z)
{
    using sim::TargetId;

    if (is_in_rect(x, z, 87, 92, 36, 41))
    {
        return TargetId::DrainMouth;
    }

    if (is_in_rect(x, z, 73, 76, 42, 50))
    {
        return TargetId::TerraceCut;
    }

    if (is_in_rect(x, z, 76, 92, 36, 54))
    {
        return TargetId::GardenBedNorth;
    }

    if (is_in_rect(x, z, 60, 88, 60, 62))
    {
        return TargetId::FlatStoneRun;
    }

    if (is_in_rect(x, z, 60, 72, 55, 58))
    {
        return TargetId::CellarEdge;
    }

    return std::nullopt;
}

[[nodiscard]] CameraMatrices build_camera_matrices(
    const gfx::OrbitCamera& camera,
    UINT width,
    UINT height)
{
    const float radians_per_degree = pi / 180.0f;
    const float yaw = camera.yaw_degrees() * radians_per_degree;
    const float pitch = camera.pitch_degrees() * radians_per_degree;
    const float cos_yaw = std::cosf(yaw);
    const float sin_yaw = std::sinf(yaw);
    const float cos_pitch = std::cosf(pitch);
    const float sin_pitch = std::sinf(pitch);

    const XMFLOAT3 target_position{
        camera.focus_x(),
        camera.focus_y(),
        camera.focus_z()};
    const XMFLOAT3 eye_position{
        target_position.x + camera.distance() * cos_yaw * cos_pitch,
        target_position.y + camera.distance() * sin_pitch,
        target_position.z + camera.distance() * sin_yaw * cos_pitch};

    CameraMatrices matrices{};
    matrices.eye_position = eye_position;
    matrices.target_position = target_position;
    matrices.view = XMMatrixLookAtLH(
        XMLoadFloat3(&eye_position),
        XMLoadFloat3(&target_position),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    matrices.projection = XMMatrixPerspectiveFovLH(
        pi / 4.0f,
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        400.0f);
    return matrices;
}

[[nodiscard]] std::filesystem::path executable_directory()
{
    std::array<wchar_t, MAX_PATH> module_path{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length >= module_path.size())
    {
        throw std::runtime_error("Could not resolve executable path.");
    }

    return std::filesystem::path(module_path.data()).parent_path();
}

[[nodiscard]] ComPtr<ID3DBlob> load_compiled_shader_blob(const std::filesystem::path& shader_path)
{
    ComPtr<ID3DBlob> shader_blob;
    throw_if_failed(
        D3DReadFileToBlob(shader_path.c_str(), &shader_blob),
        "Could not load compiled shader blob.");
    return shader_blob;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_upload_buffer(
    ID3D12Device* device,
    UINT64 byte_size,
    const char* failure_message)
{
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = std::max<UINT64>(byte_size, 4);
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    throw_if_failed(
        device->CreateCommittedResource(
            &upload_heap,
            D3D12_HEAP_FLAG_NONE,
            &buffer_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)),
        failure_message);
    return buffer;
}

[[nodiscard]] bool try_intersect_aabb(
    const Ray& ray,
    const XMFLOAT3& minimum,
    const XMFLOAT3& maximum,
    float& distance)
{
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::max();

    const auto clip_axis = [&](float origin, float direction, float axis_min, float axis_max) {
        constexpr float epsilon = 0.0001f;

        if (std::fabs(direction) < epsilon)
        {
            return origin >= axis_min && origin <= axis_max;
        }

        float enter = (axis_min - origin) / direction;
        float exit = (axis_max - origin) / direction;

        if (enter > exit)
        {
            std::swap(enter, exit);
        }

        t_min = std::max(t_min, enter);
        t_max = std::min(t_max, exit);
        return t_max >= t_min;
    };

    if (!clip_axis(ray.origin.x, ray.direction.x, minimum.x, maximum.x))
    {
        return false;
    }

    if (!clip_axis(ray.origin.y, ray.direction.y, minimum.y, maximum.y))
    {
        return false;
    }

    if (!clip_axis(ray.origin.z, ray.direction.z, minimum.z, maximum.z))
    {
        return false;
    }

    if (t_max < 0.0f)
    {
        return false;
    }

    distance = t_min >= 0.0f ? t_min : t_max;
    return true;
}

[[nodiscard]] std::string title_case_token(std::string_view token)
{
    std::string out(token);
    if (!out.empty())
    {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

[[nodiscard]] std::string humanize_snake_case(std::string_view text)
{
    std::stringstream stream;
    std::string current;
    bool first = true;

    for (const char ch : text)
    {
        if (ch == '_')
        {
            if (!current.empty())
            {
                if (!first)
                {
                    stream << " ";
                }
                stream << title_case_token(current);
                current.clear();
                first = false;
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty())
    {
        if (!first)
        {
            stream << " ";
        }
        stream << title_case_token(current);
    }

    return stream.str();
}

[[nodiscard]] std::string target_label(sim::TargetId target)
{
    return humanize_snake_case(sim::to_string(target));
}

[[nodiscard]] std::string action_label(const sim::LegalAction& action)
{
    return humanize_snake_case(action.id.view());
}

[[nodiscard]] ImVec2 target_position(sim::TargetId target, const ImVec2& min, const ImVec2& size)
{
    // Deterministic yard sketch layout for the five scenario targets.
    const auto place = [&](float nx, float ny) {
        return ImVec2(min.x + nx * size.x, min.y + ny * size.y);
    };

    switch (target)
    {
    case sim::TargetId::DrainMouth:
        return place(0.80f, 0.22f);
    case sim::TargetId::TerraceCut:
        return place(0.62f, 0.38f);
    case sim::TargetId::GardenBedNorth:
        return place(0.75f, 0.34f);
    case sim::TargetId::FlatStoneRun:
        return place(0.52f, 0.62f);
    case sim::TargetId::CellarEdge:
        return place(0.38f, 0.58f);
    }

    return place(0.50f, 0.50f);
}

[[nodiscard]] std::optional<VoxelSelection> selection_for_target(
    const sim::GrassField& field,
    sim::TargetId target)
{
    int min_x = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int min_z = std::numeric_limits<int>::max();
    int max_z = std::numeric_limits<int>::min();
    bool found = false;

    for (int z = 0; z < field.depth(); ++z)
    {
        for (int x = 0; x < field.width(); ++x)
        {
            const auto target_for_cell = scenario_target_for_cell(x, z);
            if (!target_for_cell || *target_for_cell != target)
            {
                continue;
            }

            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);
            found = true;
        }
    }

    if (!found)
    {
        return std::nullopt;
    }

    const int selection_x = (min_x + max_x) / 2;
    const int selection_z = (min_z + max_z) / 2;
    const int column_top = field.at(selection_x, selection_z).column_height_voxels;
    if (column_top <= 0)
    {
        return std::nullopt;
    }

    return VoxelSelection{selection_x, column_top - 1, selection_z};
}

[[nodiscard]] std::optional<VoxelSelection> selection_for_map_position(
    const sim::GrassField& field,
    const ImVec2& canvas_min,
    const ImVec2& canvas_size,
    const ImVec2& mouse_pos)
{
    if (canvas_size.x <= 0.0f || canvas_size.y <= 0.0f)
    {
        return std::nullopt;
    }

    const float nx = std::clamp((mouse_pos.x - canvas_min.x) / canvas_size.x, 0.0f, 0.9999f);
    const float nz = std::clamp((mouse_pos.y - canvas_min.y) / canvas_size.y, 0.0f, 0.9999f);
    const int x = std::clamp(static_cast<int>(nx * static_cast<float>(field.width())), 0, field.width() - 1);
    const int z = std::clamp(static_cast<int>(nz * static_cast<float>(field.depth())), 0, field.depth() - 1);
    const int column_top = field.at(x, z).column_height_voxels;
    if (column_top <= 0)
    {
        return std::nullopt;
    }

    return VoxelSelection{x, column_top - 1, z};
}

struct Application
{
    HWND hwnd = nullptr;
    std::optional<gfx::D3D12Context> context;
    gfx::UIFrameRenderer ui_frame_renderer;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    ComPtr<ID3D12DescriptorHeap> cbv_heap;
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> coarse_pipeline_state;
    ComPtr<ID3D12Resource> field_buffer;
    ComPtr<ID3D12Resource> refined_patch_lookup_buffer;
    ComPtr<ID3D12Resource> refined_patch_metadata_buffer;
    ComPtr<ID3D12Resource> refined_patch_height_buffer;
    ComPtr<ID3D12Resource> constant_buffer;
    SceneConstants* constant_buffer_data = nullptr;
    UINT field_buffer_capacity_cells = 0;
    UINT refined_patch_lookup_capacity = 0;
    UINT refined_patch_capacity = 0;
    UINT refined_patch_height_capacity = 0;
    UINT cbv_srv_descriptor_size = 0;
    float field_origin_x = 0.0f;
    float field_origin_z = 0.0f;
    float display_voxel_size_feet = default_voxel_size_feet;
    float max_column_height_voxels = 1.0f;

    // Rendering
    gfx::OrbitCamera camera;
    
    // UI State
    std::optional<sim::TargetId> selected_target;
    std::optional<VoxelSelection> selected_voxel;
    bool show_demo_window = false;
    bool show_voxel_inspector = true;
    bool show_agent_panel = true;
    bool show_chat_log = true;
    bool show_world_panel = true;

    std::vector<std::string> chat_lines;
    MouseState mouse{};

    // Simulation
    std::unique_ptr<playtest::GrannysYardSession> session;
    sim::GrassField field{default_field_size, default_field_size, default_voxel_size_feet};
    sim::GravityErosionField erosion_field{field};
    sim::AdaptiveTerrainOwnershipField ownership_field{field, erosion_field};
    sim::SparseRefinedPatchField refined_patch_field{field, erosion_field, ownership_field};

    Application()
        : camera(0.0f, 10.0f, 15.0f)
    {
        g_app = this;
    }

    ~Application()
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_app = nullptr;
    }

    void initialize()
    {
        create_window();
        initialize_d3d12();
        initialize_simulation();
        initialize_world_renderer();
        initialize_imgui();
    }

    void create_window()
    {
        WNDCLASSEXW wnd_class = {};
        wnd_class.cbSize = sizeof(WNDCLASSEXW);
        wnd_class.style = CS_HREDRAW | CS_VREDRAW;
        wnd_class.lpfnWndProc = window_proc;
        wnd_class.hInstance = GetModuleHandleW(nullptr);
        wnd_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wnd_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wnd_class.lpszClassName = main_window_class_name;

        RegisterClassExW(&wnd_class);

        hwnd = CreateWindowExW(
            0,
            main_window_class_name,
            L"Agent Sandbox",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1600,
            1200,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!hwnd)
            throw std::runtime_error("Failed to create window");

        ShowWindow(hwnd, SW_SHOW);
    }

    void initialize_d3d12()
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        UINT width = rect.right - rect.left;
        UINT height = rect.bottom - rect.top;
        context.emplace(hwnd, width, height, frame_count);

        // Create SRV heap for ImGui
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = 1;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(context->device()->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap))))
            throw std::runtime_error("Failed to create SRV heap");

        D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
        cbv_heap_desc.NumDescriptors = 4;
        cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        throw_if_failed(
            context->device()->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap)),
            "Could not create world CBV heap.");
        cbv_srv_descriptor_size = context->device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void initialize_world_renderer()
    {
        const std::filesystem::path shader_directory = executable_directory() / L"shaders";
        const ComPtr<ID3DBlob> vertex_shader = load_compiled_shader_blob(shader_directory / L"grass_field_vs.cso");
        const ComPtr<ID3DBlob> coarse_pixel_shader = load_compiled_shader_blob(shader_directory / L"grass_field_coarse_ps.cso");

        D3D12_DESCRIPTOR_RANGE descriptor_range{};
        descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptor_range.NumDescriptors = 4;
        descriptor_range.BaseShaderRegister = 0;
        descriptor_range.RegisterSpace = 0;
        descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_parameters[2]{};
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameters[0].Descriptor.ShaderRegister = 0;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc{};
        root_signature_desc.NumParameters = static_cast<UINT>(std::size(root_parameters));
        root_signature_desc.pParameters = root_parameters;
        root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> errors;
        throw_if_failed(
            D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
            "Could not serialize root signature.");
        throw_if_failed(
            context->device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)),
            "Could not create root signature.");

        D3D12_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer.DepthClipEnable = TRUE;

        D3D12_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.InputLayout = {nullptr, 0};
        pipeline_desc.pRootSignature = root_signature.Get();
        pipeline_desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        pipeline_desc.PS = {coarse_pixel_shader->GetBufferPointer(), coarse_pixel_shader->GetBufferSize()};
        pipeline_desc.RasterizerState = rasterizer;
        pipeline_desc.BlendState = blend;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1;
        throw_if_failed(
            context->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&coarse_pipeline_state)),
            "Could not create graphics pipeline.");

        const UINT64 constant_buffer_size = (sizeof(SceneConstants) + 255) & ~255;
        constant_buffer = create_upload_buffer(context->device(), constant_buffer_size, "Could not create constant buffer.");
        D3D12_RANGE read_range{0, 0};
        throw_if_failed(
            constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data)),
            "Could not map constant buffer.");

        field_buffer_capacity_cells = static_cast<UINT>(field.cell_count());
        field_buffer = create_upload_buffer(
            context->device(),
            static_cast<UINT64>(field_buffer_capacity_cells) * sizeof(GpuFieldCell),
            "Could not create field buffer.");

        refined_patch_lookup_capacity = static_cast<UINT>(field.cell_count());
        refined_patch_capacity = std::max(1, refined_patch_field.patch_count());
        refined_patch_height_capacity = std::max(
            1u,
            refined_patch_capacity * static_cast<UINT>(refined_patch_field.patch_resolution() * refined_patch_field.patch_resolution()));

        refined_patch_lookup_buffer = create_upload_buffer(
            context->device(),
            static_cast<UINT64>(refined_patch_lookup_capacity) * sizeof(std::int32_t),
            "Could not create refined patch lookup buffer.");
        refined_patch_metadata_buffer = create_upload_buffer(
            context->device(),
            static_cast<UINT64>(refined_patch_capacity) * sizeof(GpuRefinedPatchMetadata),
            "Could not create refined patch metadata buffer.");
        refined_patch_height_buffer = create_upload_buffer(
            context->device(),
            static_cast<UINT64>(refined_patch_height_capacity) * sizeof(std::uint32_t),
            "Could not create refined patch height buffer.");

        rebuild_world_field_buffer();
    }

    void rebuild_world_field_buffer()
    {
        std::vector<GpuFieldCell> cells;
        cells.reserve(field.cell_count());
        max_column_height_voxels = 0.0f;

        for (int z = 0; z < field.depth(); ++z)
        {
            for (int x = 0; x < field.width(); ++x)
            {
                const auto& cell = field.at(x, z);
                max_column_height_voxels = std::max(max_column_height_voxels, static_cast<float>(cell.column_height_voxels));

                GpuFieldCell gpu_cell{
                    .column_height_voxels = static_cast<float>(cell.column_height_voxels),
                    .material = static_cast<std::uint32_t>(cell.material),
                    .is_homestead_pad = cell.is_homestead_pad ? 1u : 0u,
                    .is_garden_bed = cell.garden.is_garden_bed ? 1u : 0u,
                    .soil_moisture = cell.garden.soil_moisture,
                    .fertility = cell.garden.fertility,
                    .sunlight = cell.garden.sunlight,
                    .weed_pressure = cell.garden.weed_pressure,
                    .coarse_full_height_voxels = static_cast<float>(cell.column_height_voxels),
                };

                const auto target = scenario_target_for_cell(x, z);
                if (target == sim::TargetId::GardenBedNorth && session->scenario().state().garden_bed_north_watered)
                {
                    gpu_cell.soil_moisture = 0.95f;
                    gpu_cell.fertility = std::max(gpu_cell.fertility, 0.94f);
                }
                else if (target == sim::TargetId::CellarEdge && session->scenario().state().cellar_edge_saturated)
                {
                    gpu_cell.material = static_cast<std::uint32_t>(sim::TerrainMaterial::WetSoil);
                    gpu_cell.soil_moisture = 0.98f;
                }
                else if (target == sim::TargetId::FlatStoneRun && session->scenario().state().path_edge_softened)
                {
                    gpu_cell.soil_moisture = 0.88f;
                    gpu_cell.weed_pressure = std::max(gpu_cell.weed_pressure, 0.20f);
                }
                else if (target == sim::TargetId::DrainMouth && session->scenario().state().drain_source_routed)
                {
                    gpu_cell.soil_moisture = 1.00f;
                }

                cells.push_back(gpu_cell);
            }
        }

        field_origin_x = -static_cast<float>(field.width()) * field.voxel_size_feet() * 0.5f;
        field_origin_z = -static_cast<float>(field.depth()) * field.voxel_size_feet() * 0.5f;
        display_voxel_size_feet = field.voxel_size_feet();

        void* field_buffer_data = nullptr;
        D3D12_RANGE read_range{0, 0};
        throw_if_failed(field_buffer->Map(0, &read_range, &field_buffer_data), "Could not map field buffer.");
        std::memcpy(field_buffer_data, cells.data(), cells.size() * sizeof(GpuFieldCell));
        field_buffer->Unmap(0, nullptr);

        const std::size_t lookup_count = static_cast<std::size_t>(field.width() * field.depth());
        std::vector<std::int32_t> lookup(lookup_count, -1);
        if (lookup.empty())
        {
            lookup.push_back(-1);
        }
        void* lookup_data = nullptr;
        throw_if_failed(refined_patch_lookup_buffer->Map(0, &read_range, &lookup_data), "Could not map refined patch lookup buffer.");
        std::memcpy(lookup_data, lookup.data(), lookup.size() * sizeof(std::int32_t));
        refined_patch_lookup_buffer->Unmap(0, nullptr);

        std::vector<GpuRefinedPatchMetadata> metadata(1);
        void* metadata_data = nullptr;
        throw_if_failed(refined_patch_metadata_buffer->Map(0, &read_range, &metadata_data), "Could not map refined patch metadata buffer.");
        std::memcpy(metadata_data, metadata.data(), metadata.size() * sizeof(GpuRefinedPatchMetadata));
        refined_patch_metadata_buffer->Unmap(0, nullptr);

        std::vector<std::uint32_t> heights(1, 0u);
        void* heights_data = nullptr;
        throw_if_failed(refined_patch_height_buffer->Map(0, &read_range, &heights_data), "Could not map refined patch height buffer.");
        std::memcpy(heights_data, heights.data(), heights.size() * sizeof(std::uint32_t));
        refined_patch_height_buffer->Unmap(0, nullptr);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = cbv_heap->GetCPUDescriptorHandleForHeapStart();
        srv_desc.Buffer.NumElements = static_cast<UINT>(cells.size());
        srv_desc.Buffer.StructureByteStride = sizeof(GpuFieldCell);
        context->device()->CreateShaderResourceView(field_buffer.Get(), &srv_desc, descriptor);

        descriptor.ptr += cbv_srv_descriptor_size;
        srv_desc.Buffer.NumElements = static_cast<UINT>(lookup.size());
        srv_desc.Buffer.StructureByteStride = sizeof(std::int32_t);
        context->device()->CreateShaderResourceView(refined_patch_lookup_buffer.Get(), &srv_desc, descriptor);

        descriptor.ptr += cbv_srv_descriptor_size;
        srv_desc.Buffer.NumElements = static_cast<UINT>(metadata.size());
        srv_desc.Buffer.StructureByteStride = sizeof(GpuRefinedPatchMetadata);
        context->device()->CreateShaderResourceView(refined_patch_metadata_buffer.Get(), &srv_desc, descriptor);

        descriptor.ptr += cbv_srv_descriptor_size;
        srv_desc.Buffer.NumElements = static_cast<UINT>(heights.size());
        srv_desc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        context->device()->CreateShaderResourceView(refined_patch_height_buffer.Get(), &srv_desc, descriptor);
    }

    void update_world_constants(UINT width, UINT height)
    {
        const CameraMatrices matrices = build_camera_matrices(camera, width, height);
        const XMMATRIX inverse_view_projection = XMMatrixTranspose(XMMatrixInverse(nullptr, matrices.view * matrices.projection));
        XMStoreFloat4x4(&constant_buffer_data->inverse_view_projection, inverse_view_projection);
        constant_buffer_data->camera_world_position = {
            matrices.eye_position.x,
            matrices.eye_position.y,
            matrices.eye_position.z,
            1.0f};
        constant_buffer_data->field_origin_and_voxel_size = {
            field_origin_x,
            field_origin_z,
            display_voxel_size_feet,
            max_column_height_voxels};
        constant_buffer_data->field_info = {
            static_cast<std::uint32_t>(field.width()),
            static_cast<std::uint32_t>(field.depth()),
            0u,
            selected_voxel ? 1u : 0u};
        constant_buffer_data->selection_info = {
            selected_voxel ? static_cast<std::uint32_t>(selected_voxel->x) : 0u,
            selected_voxel ? static_cast<std::uint32_t>(selected_voxel->y) : 0u,
            selected_voxel ? static_cast<std::uint32_t>(selected_voxel->z) : 0u,
            0u};
        constant_buffer_data->refinement_info = {
            static_cast<std::uint32_t>(erosion_field.patch_resolution()),
            0u,
            0u,
            0u};
        constant_buffer_data->display_info = {0u, 0u, 0u, 0u};
    }

    void initialize_imgui()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(context->device(), frame_count, DXGI_FORMAT_R8G8B8A8_UNORM, srv_heap.Get(),
                            srv_heap->GetCPUDescriptorHandleForHeapStart(),
                            srv_heap->GetGPUDescriptorHandleForHeapStart());
    }

    void initialize_simulation()
    {
        session = std::make_unique<playtest::GrannysYardSession>();
        chat_lines.push_back("Session initialized.");
    }

    void render_frame()
    {
        // Start ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        update_mouse_controls();

        // Render UI panels
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        render_world_panel();
        render_voxel_inspector();
        render_agent_panel();
        render_chat_log();

        ImGui::Render();

        RECT rect;
        GetClientRect(hwnd, &rect);
        const UINT width = static_cast<UINT>(std::max(1L, rect.right - rect.left));
        const UINT height = static_cast<UINT>(std::max(1L, rect.bottom - rect.top));
        rebuild_world_field_buffer();
        update_world_constants(width, height);

        const float clear_color[4] = {0.08f, 0.10f, 0.12f, 1.0f};
        ui_frame_renderer.render(
            *context,
            nullptr,
            clear_color,
            [&](ID3D12GraphicsCommandList* command_list) {
                D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
                D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
                command_list->RSSetViewports(1, &viewport);
                command_list->RSSetScissorRects(1, &scissor);
                command_list->SetPipelineState(coarse_pipeline_state.Get());
                command_list->SetGraphicsRootSignature(root_signature.Get());
                ID3D12DescriptorHeap* world_heaps[] = {cbv_heap.Get()};
                command_list->SetDescriptorHeaps(1, world_heaps);
                command_list->SetGraphicsRootConstantBufferView(0, constant_buffer->GetGPUVirtualAddress());
                command_list->SetGraphicsRootDescriptorTable(1, cbv_heap->GetGPUDescriptorHandleForHeapStart());
                command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                command_list->DrawInstanced(3, 1, 0, 0);

                ID3D12DescriptorHeap* imgui_heaps[] = {srv_heap.Get()};
                command_list->SetDescriptorHeaps(1, imgui_heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
            });
    }

    void on_mouse_down(MouseState::DragMode drag_mode, const POINT& point)
    {
        mouse.left_button_down = drag_mode == MouseState::DragMode::pan;
        mouse.right_button_down = drag_mode == MouseState::DragMode::orbit;
        mouse.dragging = false;
        mouse.drag_mode = drag_mode;
        mouse.anchor_position = point;
        mouse.last_position = point;
        SetFocus(hwnd);
        SetCapture(hwnd);
    }

    void on_mouse_up(MouseState::DragMode drag_mode)
    {
        if ((drag_mode == MouseState::DragMode::pan && mouse.left_button_down) ||
            (drag_mode == MouseState::DragMode::orbit && mouse.right_button_down))
        {
            mouse.left_button_down = false;
            mouse.right_button_down = false;
            mouse.dragging = false;
            mouse.drag_mode = MouseState::DragMode::none;
            ReleaseCapture();
        }
    }

    void on_mouse_move(const POINT& point, UINT width, UINT height)
    {
        const bool is_panning = mouse.drag_mode == MouseState::DragMode::pan && mouse.left_button_down;
        const bool is_orbiting = mouse.drag_mode == MouseState::DragMode::orbit && mouse.right_button_down;
        if (!is_panning && !is_orbiting)
        {
            return;
        }

        if (!mouse.dragging)
        {
            const int drag_distance_x = std::abs(point.x - mouse.anchor_position.x);
            const int drag_distance_y = std::abs(point.y - mouse.anchor_position.y);
            mouse.dragging = drag_distance_x >= 4 || drag_distance_y >= 4;
        }

        if (!mouse.dragging)
        {
            return;
        }

        const int dx = point.x - mouse.last_position.x;
        const int dy = point.y - mouse.last_position.y;
        mouse.last_position = point;

        if (is_panning)
        {
            const CameraMatrices matrices = build_camera_matrices(camera, width, height);
            XMVECTOR forward = XMVectorSubtract(
                XMLoadFloat3(&matrices.target_position),
                XMLoadFloat3(&matrices.eye_position));
            forward = XMVectorSetY(forward, 0.0f);
            forward = XMVector3Normalize(forward);
            const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
            const float pan_scale = camera.distance()
                / static_cast<float>(std::max<UINT>(1, std::min(width, height)))
                * 1.9f;
            const XMVECTOR pan_delta = XMVectorAdd(
                XMVectorScale(right, static_cast<float>(-dx) * pan_scale),
                XMVectorScale(forward, static_cast<float>(dy) * pan_scale));

            XMFLOAT3 delta{};
            XMStoreFloat3(&delta, pan_delta);
            camera.move_focus(delta.x, 0.0f, delta.z);
            return;
        }

        camera.orbit(
            static_cast<float>(-dx) * 0.35f,
            static_cast<float>(dy) * 0.25f);
    }

    void on_mouse_wheel(float wheel_steps)
    {
        // Match 001 sensitivity: one wheel notch equals ~1.2 zoom delta.
        camera.zoom(-wheel_steps * 1.2f);
    }

    [[nodiscard]] std::optional<VoxelSelection> try_pick_voxel(const POINT& client_point, UINT width, UINT height) const
    {
        const CameraMatrices matrices = build_camera_matrices(camera, width, height);
        const XMMATRIX world = XMMatrixIdentity();
        const XMVECTOR far_screen = XMVectorSet(
            static_cast<float>(client_point.x),
            static_cast<float>(client_point.y),
            1.0f,
            1.0f);
        const XMVECTOR far_world = XMVector3Unproject(
            far_screen,
            0.0f,
            0.0f,
            static_cast<float>(width),
            static_cast<float>(height),
            0.0f,
            1.0f,
            matrices.projection,
            matrices.view,
            world);
        const XMVECTOR eye = XMLoadFloat3(&matrices.eye_position);
        const XMVECTOR direction = XMVector3Normalize(far_world - eye);

        Ray ray{};
        XMStoreFloat3(&ray.origin, eye);
        XMStoreFloat3(&ray.direction, direction);

        const float voxel_size = field.voxel_size_feet();
        const int grid_width = field.width();
        const int grid_depth = field.depth();

        float field_distance = 0.0f;
        const XMFLOAT3 field_min{field_origin_x, 0.0f, field_origin_z};
        const XMFLOAT3 field_max{
            field_origin_x + static_cast<float>(grid_width) * voxel_size,
            max_column_height_voxels * voxel_size,
            field_origin_z + static_cast<float>(grid_depth) * voxel_size};
        if (!try_intersect_aabb(ray, field_min, field_max, field_distance))
        {
            return std::nullopt;
        }

        float current_t = std::max(field_distance, 0.0f);
        const XMFLOAT3 current_position{
            ray.origin.x + ray.direction.x * (current_t + 0.0005f),
            ray.origin.y + ray.direction.y * (current_t + 0.0005f),
            ray.origin.z + ray.direction.z * (current_t + 0.0005f)};
        int cell_x = std::clamp(
            static_cast<int>(std::floor((current_position.x - field_origin_x) / voxel_size)),
            0,
            grid_width - 1);
        int cell_z = std::clamp(
            static_cast<int>(std::floor((current_position.z - field_origin_z) / voxel_size)),
            0,
            grid_depth - 1);

        const int step_x = ray.direction.x > 0.0f ? 1 : (ray.direction.x < 0.0f ? -1 : 0);
        const int step_z = ray.direction.z > 0.0f ? 1 : (ray.direction.z < 0.0f ? -1 : 0);
        const float large_t = 1.0e20f;
        float t_max_x = large_t;
        float t_max_z = large_t;
        float t_delta_x = large_t;
        float t_delta_z = large_t;

        if (step_x != 0)
        {
            const float next_boundary_x =
                field_origin_x + static_cast<float>(step_x > 0 ? (cell_x + 1) : cell_x) * voxel_size;
            t_max_x = (next_boundary_x - ray.origin.x) / ray.direction.x;
            t_delta_x = voxel_size / std::fabs(ray.direction.x);
        }

        if (step_z != 0)
        {
            const float next_boundary_z =
                field_origin_z + static_cast<float>(step_z > 0 ? (cell_z + 1) : cell_z) * voxel_size;
            t_max_z = (next_boundary_z - ray.origin.z) / ray.direction.z;
            t_delta_z = voxel_size / std::fabs(ray.direction.z);
        }

        const int max_steps = grid_width + grid_depth + 8;
        for (int iteration = 0; iteration < max_steps; ++iteration)
        {
            if (cell_x < 0 || cell_x >= grid_width || cell_z < 0 || cell_z >= grid_depth)
            {
                break;
            }

            const float cell_exit_t = std::min(t_max_x, t_max_z);
            const auto& cell = field.at(cell_x, cell_z);
            const float column_top = static_cast<float>(cell.column_height_voxels) * voxel_size;
            if (column_top > 0.0001f)
            {
                const float y_at_entry = ray.origin.y + current_t * ray.direction.y;
                const float y_at_exit = ray.origin.y + cell_exit_t * ray.direction.y;
                const float segment_min_y = std::min(y_at_entry, y_at_exit);
                const float segment_max_y = std::max(y_at_entry, y_at_exit);

                if (segment_max_y >= 0.0f && segment_min_y <= column_top)
                {
                    float hit_t = current_t;
                    if (ray.direction.y > 0.0f && y_at_entry < 0.0f)
                    {
                        hit_t = std::max(hit_t, (0.0f - ray.origin.y) / ray.direction.y);
                    }
                    if (ray.direction.y < 0.0f && y_at_entry > column_top)
                    {
                        hit_t = std::max(hit_t, (column_top - ray.origin.y) / ray.direction.y);
                    }

                    if (hit_t <= cell_exit_t + 0.0001f)
                    {
                        const float hit_y = ray.origin.y + ray.direction.y * hit_t;
                        const int voxel_y = std::clamp(
                            static_cast<int>(std::floor(std::max(hit_y - 0.001f, 0.0f) / voxel_size)),
                            0,
                            cell.column_height_voxels - 1);
                        return VoxelSelection{cell_x, voxel_y, cell_z};
                    }
                }
            }

            if (t_max_x < t_max_z)
            {
                current_t = t_max_x;
                t_max_x += t_delta_x;
                cell_x += step_x;
            }
            else
            {
                current_t = t_max_z;
                t_max_z += t_delta_z;
                cell_z += step_z;
            }
        }

        return std::nullopt;
    }

    void update_mouse_controls()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
        {
            return;
        }

        RECT rect{};
        GetClientRect(hwnd, &rect);
        const UINT width = static_cast<UINT>(std::max(1L, rect.right - rect.left));
        const UINT height = static_cast<UINT>(std::max(1L, rect.bottom - rect.top));

        const POINT point{
            static_cast<LONG>(io.MousePos.x),
            static_cast<LONG>(io.MousePos.y)};

        const bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);

        if (left_down && !mouse.left_button_down)
        {
            on_mouse_down(MouseState::DragMode::pan, point);
        }
        if (right_down && !mouse.right_button_down)
        {
            on_mouse_down(MouseState::DragMode::orbit, point);
        }

        if (!left_down && mouse.left_button_down)
        {
            const bool was_click = !mouse.dragging;
            on_mouse_up(MouseState::DragMode::pan);
            if (was_click)
            {
                if (const auto picked = try_pick_voxel(point, width, height))
                {
                    selected_voxel = picked;
                    selected_target = scenario_target_for_cell(picked->x, picked->z);
                    chat_lines.push_back(
                        "Selected voxel: ("
                        + std::to_string(picked->x)
                        + ", "
                        + std::to_string(picked->y)
                        + ", "
                        + std::to_string(picked->z)
                        + ")");
                }
            }
        }
        if (!right_down && mouse.right_button_down)
        {
            on_mouse_up(MouseState::DragMode::orbit);
        }

        on_mouse_move(point, width, height);

        if (std::abs(io.MouseWheel) > 0.001f)
        {
            on_mouse_wheel(io.MouseWheel);
        }
    }

    void render_world_panel()
    {
        if (!session)
        {
            return;
        }

        if (!ImGui::Begin("World", &show_world_panel))
        {
            ImGui::End();
            return;
        }

        const auto packet = session->turn_packet(selected_target);
        const auto state = session->scenario().state();

        ImGui::TextWrapped("%s", packet.objective.view().data());
        ImGui::Separator();
        ImGui::Text("Round: %s", playtest::round_result_name(packet.round_result).data());
        ImGui::Text("Turn: %u", state.turn_count);
        ImGui::Text("Sim step: %u", state.simulation_step_count);
        ImGui::Text("Hidden link: %s", state.hidden_cross_link_revealed ? "revealed" : "unknown");
        ImGui::SeparatorText("Yard Map");

        const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_size(std::max(420.0f, ImGui::GetContentRegionAvail().x), 280.0f);
        ImGui::InvisibleButton("world_map_canvas", canvas_size);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 bg = IM_COL32(22, 34, 48, 255);
        const ImU32 border = IM_COL32(65, 95, 118, 255);
        draw->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), bg, 6.0f);
        draw->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), border, 6.0f, 0, 2.0f);

        for (const auto& target : packet.visible_targets)
        {
            const ImVec2 pos = target_position(target.id, canvas_pos, canvas_size);
            const bool is_selected = selected_target && *selected_target == target.id;
            const float radius = is_selected ? 11.0f : 8.0f;
            const ImU32 color = is_selected ? IM_COL32(255, 197, 92, 255) : IM_COL32(121, 197, 255, 255);

            draw->AddCircleFilled(pos, radius, color);
            draw->AddText(ImVec2(pos.x + 12.0f, pos.y - 8.0f), IM_COL32(230, 235, 240, 255), target_label(target.id).c_str());
        }

        if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            float best_sq = 26.0f * 26.0f;
            std::optional<sim::TargetId> best_target;

            for (const auto& target : packet.visible_targets)
            {
                const ImVec2 pos = target_position(target.id, canvas_pos, canvas_size);
                const float dx = mouse_pos.x - pos.x;
                const float dy = mouse_pos.y - pos.y;
                const float dist_sq = dx * dx + dy * dy;
                if (dist_sq < best_sq)
                {
                    best_sq = dist_sq;
                    best_target = target.id;
                }
            }

            if (best_target)
            {
                selected_target = best_target;
                selected_voxel = selection_for_target(field, *best_target);
                chat_lines.push_back("Selected target: " + target_label(*best_target));
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                selected_voxel = selection_for_map_position(field, canvas_pos, canvas_size, mouse_pos);
                if (selected_voxel)
                {
                    selected_target = scenario_target_for_cell(selected_voxel->x, selected_voxel->z);
                    chat_lines.push_back(
                        "Selected voxel: ("
                        + std::to_string(selected_voxel->x)
                        + ", "
                        + std::to_string(selected_voxel->y)
                        + ", "
                        + std::to_string(selected_voxel->z)
                        + ")");
                }
            }
        }

        ImGui::SeparatorText("Controls");
        const auto legal = session->legal_actions(selected_target);
        if (legal.empty())
        {
            ImGui::TextUnformatted("No legal actions for current focus.");
        }
        else
        {
            for (const auto& action : legal)
            {
                const std::string label = action_label(action);
                if (ImGui::Button(label.c_str(), ImVec2(220.0f, 0.0f)))
                {
                    const auto outcome = session->run_action(action.id.view(), selected_target);
                    chat_lines.push_back("Action: " + label + (outcome.success ? " (ok)" : " (failed)"));
                    for (const auto& obs : outcome.observations)
                    {
                        chat_lines.push_back("- " + obs);
                    }
                }
                if (action.target)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", target_label(*action.target).c_str());
                }
            }
        }

        ImGui::End();
    }

    void render_voxel_inspector()
    {
        if (!ImGui::Begin("Voxel Inspector", &show_voxel_inspector))
        {
            ImGui::End();
            return;
        }

        if (!session)
        {
            ImGui::TextUnformatted("Session unavailable.");
            ImGui::End();
            return;
        }

        if (selected_voxel)
        {
            const auto& selection = *selected_voxel;
            const auto& cell = field.at(selection.x, selection.z);
            ImGui::Text("Voxel: (%d, %d, %d)", selection.x, selection.y, selection.z);
            ImGui::Text("Material: %s", sim::terrain_material_name(cell.material).data());
            ImGui::Text("Column height: %d", cell.column_height_voxels);
            ImGui::Text("Soil moisture: %.2f", cell.garden.soil_moisture);
            ImGui::Text("Fertility: %.2f", cell.garden.fertility);
            ImGui::Text("Sunlight: %.2f", cell.garden.sunlight);
            ImGui::Text("Weed pressure: %.2f", cell.garden.weed_pressure);
            ImGui::Text("Garden bed: %s", cell.garden.is_garden_bed ? "yes" : "no");
            ImGui::Text("Homestead pad: %s", cell.is_homestead_pad ? "yes" : "no");
            ImGui::Separator();
        }

        if (selected_target)
        {
            ImGui::Text("Target: %s", target_label(*selected_target).c_str());
            const auto packet = session->turn_packet(selected_target);
            auto found = std::find_if(
                packet.visible_targets.begin(),
                packet.visible_targets.end(),
                [&](const sim::VisibleTarget& t) { return t.id == *selected_target; });

            if (found != packet.visible_targets.end())
            {
                ImGui::Text("Kind: %s", sim::to_string(found->kind).data());
                ImGui::SeparatorText("State Tags");
                if (found->states.empty())
                {
                    ImGui::TextUnformatted("(no state tags)");
                }
                for (const auto tag : found->states)
                {
                    ImGui::BulletText("%s", sim::to_string(tag).data());
                }
            }
        }
        else
        {
            ImGui::TextUnformatted("(Click the map to inspect a voxel)");
        }

        ImGui::End();
    }

    void render_agent_panel()
    {
        if (!ImGui::Begin("Agents", &show_agent_panel))
        {
            ImGui::End();
            return;
        }

        if (!session)
        {
            ImGui::TextUnformatted("Session unavailable.");
            ImGui::End();
            return;
        }

        const auto packet = session->turn_packet(selected_target);
        const std::string role = humanize_snake_case(playtest::to_string(packet.role));
        ImGui::Text("Active role: %s", role.c_str());
        ImGui::Separator();
        ImGui::Text("Visible targets: %d", static_cast<int>(packet.visible_targets.size()));
        for (const auto& target : packet.visible_targets)
        {
            ImGui::BulletText("%s", target_label(target.id).c_str());
        }

        ImGui::End();
    }

    void render_chat_log()
    {
        if (!ImGui::Begin("Conversation Log", &show_chat_log))
        {
            ImGui::End();
            return;
        }

        if (chat_lines.empty())
        {
            ImGui::TextUnformatted("No events yet.");
        }
        else
        {
            for (const auto& line : chat_lines)
            {
                ImGui::TextWrapped("%s", line.c_str());
            }
            if (chat_lines.size() > 200)
            {
                chat_lines.erase(chat_lines.begin(), chat_lines.begin() + 100);
            }
        }
        
        ImGui::End();
    }

    void run()
    {
        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            else
            {
                render_frame();
            }
        }
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        Application app;
        app.initialize();
        app.run();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR);
        return 1;
    }

    return 0;
}
