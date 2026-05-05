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

#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/gfx/orbit_camera.h"
#include "grannys_house_trials/gfx/d3d12_context.h"
#include "grannys_house_trials/gfx/ui_frame_renderer.h"
#include "grannys_house_trials/gfx/device_resources.h"
#include "grannys_house_trials/playtest/agent_command.h"
#include "grannys_house_trials/playtest/evidence_board_panel.h"
#include "grannys_house_trials/playtest/grannys_yard_session.h"
#include "grannys_house_trials/playtest/evidence_board_view.h"
#include "grannys_house_trials/playtest/turn_packet.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"
#include "grannys_house_trials/sim/sparse_refined_patch_field.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
using namespace DirectX;

constexpr UINT frame_count = 2;
constexpr float pi = 3.1415926535f;
constexpr wchar_t main_window_class_name[] = L"GrannysHouseTrialsGrassField001";
constexpr wchar_t viewport_window_class_name[] = L"GrannysHouseTrialsGrassFieldViewport";
constexpr int default_field_size = 100;
constexpr float default_voxel_size_feet = 1.0f;

enum ControlId : int
{
    control_id_reset_camera = 1001,
    control_id_clear_selection = 1002,
    control_id_toggle_highlight = 1003,
    control_id_copy_agent_snapshot = 1004,
    control_id_step_erosion = 1005,
    control_id_display_grid_combo = 1006,
    control_id_scenario_action_combo = 1007,
    control_id_run_action = 1008,
    control_id_advance_round = 1009,
    control_id_end_round = 1014,
    control_id_reset_round = 1010,
    control_id_field_size_combo = 1011,
    control_id_toggle_fog = 1012,
    control_id_toggle_packet_view = 1013,
    control_id_run_agent_json = 1015,
};

enum class DisplayGridMode
{
    coarse_foot_columns,
    erosion_inch_columns,
    hybrid_adaptive_columns,
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

struct FrameContext
{
    ComPtr<ID3D12CommandAllocator> allocator;
    UINT64 fence_value = 0;
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

struct VoxelSelection
{
    int x = 0;
    int y = 0;
    int z = 0;
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

[[nodiscard]] constexpr std::string_view display_grid_mode_name(DisplayGridMode mode) noexcept
{
    switch (mode)
    {
    case DisplayGridMode::coarse_foot_columns:
        return "1-foot coarse columns";
    case DisplayGridMode::erosion_inch_columns:
        return "1-inch refined remainder";
    case DisplayGridMode::hybrid_adaptive_columns:
        return "hybrid adaptive columns";
    }

    return "unknown display grid";
}

[[nodiscard]] std::wstring legal_action_label(const grannys_house_trials::sim::LegalAction &action)
{
    using grannys_house_trials::sim::ActionKind;

    switch (action.kind)
    {
    case ActionKind::Inspect:
        return L"Inspect target";
    case ActionKind::InspectNeighborhood:
        return L"Inspect nearby area";
    case ActionKind::RouteWater:
        return L"Route water source";
    case ActionKind::DigChannel:
        return L"Dig shallow channel";
    case ActionKind::PackSoil:
        return L"Pack soil";
    case ActionKind::AdvanceSimulation:
        return L"Advance round";
    case ActionKind::ResetRound:
        return L"Reset round";
    case ActionKind::Look:
        return L"Look";
    case ActionKind::Wait:
        return L"Wait";
    }

    return std::wstring(action.id.view().begin(), action.id.view().end());
}

[[nodiscard]] constexpr std::string_view tester_role_name(grannys_house_trials::playtest::TesterRole role) noexcept
{
    using grannys_house_trials::playtest::TesterRole;

    switch (role)
    {
    case TesterRole::Builder:
        return "Builder";
    case TesterRole::ChaosTester:
        return "Chaos Tester";
    case TesterRole::SystemsAuditor:
        return "Systems Auditor";
    }

    return "Unknown";
}

[[nodiscard]] bool is_in_rect(int x, int z, int min_x, int max_x, int min_z, int max_z) noexcept
{
    return x >= min_x && x <= max_x && z >= min_z && z <= max_z;
}

[[nodiscard]] std::optional<grannys_house_trials::sim::TargetId> scenario_target_for_cell(int x, int z)
{
    using grannys_house_trials::sim::TargetId;

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

[[nodiscard]] constexpr std::string_view terrain_volume_ownership_name(
    grannys_house_trials::sim::TerrainVolumeOwnership ownership) noexcept
{
    using grannys_house_trials::sim::TerrainVolumeOwnership;

    switch (ownership)
    {
    case TerrainVolumeOwnership::empty:
        return "empty";
    case TerrainVolumeOwnership::coarse_full_block:
        return "coarse_full_block";
    case TerrainVolumeOwnership::refined_inch_volume:
        return "refined_inch_volume";
    }

    return "unknown";
}

[[nodiscard]] bool system_prefers_dark_mode()
{
    DWORD light_theme_value = 1;
    DWORD value_size = sizeof(light_theme_value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &light_theme_value,
        &value_size);

    return status == ERROR_SUCCESS && light_theme_value == 0;
}

void apply_dark_title_bar_if_available(HWND hwnd)
{
    const BOOL use_dark_mode = system_prefers_dark_mode() ? TRUE : FALSE;
    constexpr DWORD dwmwa_use_immersive_dark_mode = 20;
    DwmSetWindowAttribute(
        hwnd,
        dwmwa_use_immersive_dark_mode,
        &use_dark_mode,
        sizeof(use_dark_mode));
}

void apply_explorer_theme(HWND hwnd)
{
    SetWindowTheme(hwnd, L"Explorer", nullptr);
}

void throw_if_failed(HRESULT result, const char *message)
{
    if (FAILED(result))
    {
        throw std::runtime_error(message);
    }
}

void debug_log(std::string_view message)
{
    std::string line(message);
    line += '\n';
    OutputDebugStringA(line.c_str());
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

[[nodiscard]] ComPtr<ID3DBlob> load_compiled_shader_blob(const std::filesystem::path &shader_path)
{
    {
        std::ostringstream stream;
        stream << "[grass-field-001] loading compiled shader " << shader_path.string();
        debug_log(stream.str());
    }

    ComPtr<ID3DBlob> shader_blob;
    throw_if_failed(
        D3DReadFileToBlob(shader_path.c_str(), &shader_blob),
        "Could not load compiled shader blob.");
    return shader_blob;
}

[[nodiscard]] std::wstring widen(std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}

[[nodiscard]] grannys_house_trials::gfx::OrbitCamera make_default_camera(int field_width, int field_depth)
{
    const float field_span = static_cast<float>(std::max(field_width, field_depth));
    return grannys_house_trials::gfx::OrbitCamera{45.0f, 35.0f, std::max(140.0f, field_span * 1.15f)};
}

[[nodiscard]] std::string escape_json_string(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (char character : text)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

[[nodiscard]] bool copy_text_to_clipboard(HWND owner, const std::wstring &text)
{
    if (!OpenClipboard(owner))
    {
        return false;
    }

    EmptyClipboard();

    const std::size_t byte_count = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL global_handle = GlobalAlloc(GMEM_MOVEABLE, byte_count);

    if (!global_handle)
    {
        CloseClipboard();
        return false;
    }

    void *memory = GlobalLock(global_handle);
    if (!memory)
    {
        GlobalFree(global_handle);
        CloseClipboard();
        return false;
    }

    std::memcpy(memory, text.c_str(), byte_count);
    GlobalUnlock(global_handle);

    if (!SetClipboardData(CF_UNICODETEXT, global_handle))
    {
        GlobalFree(global_handle);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

[[nodiscard]] std::optional<std::wstring> read_text_from_clipboard(HWND owner)
{
    if (!OpenClipboard(owner))
    {
        return std::nullopt;
    }

    HGLOBAL clipboard_data = GetClipboardData(CF_UNICODETEXT);
    if (!clipboard_data)
    {
        CloseClipboard();
        return std::nullopt;
    }

    const wchar_t *text = static_cast<const wchar_t *>(GlobalLock(clipboard_data));
    if (!text)
    {
        CloseClipboard();
        return std::nullopt;
    }

    const std::wstring result(text);
    GlobalUnlock(clipboard_data);
    CloseClipboard();
    return result;
}

[[nodiscard]] std::optional<std::string> narrow_from_wide(std::wstring_view text)
{
    if (text.empty())
    {
        return std::string{};
    }

    const int buffer_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (buffer_size <= 0)
    {
        return std::nullopt;
    }

    std::string result(static_cast<std::size_t>(buffer_size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        buffer_size,
        nullptr,
        nullptr);

    return result;
}

[[nodiscard]] std::vector<GpuFieldCell> build_coarse_field_buffer_data(
    const grannys_house_trials::sim::GrassField &field,
    const grannys_house_trials::sim::GrannysYardScenario &scenario,
    float &max_column_height_voxels)
{
    std::vector<GpuFieldCell> cells;
    cells.reserve(field.cell_count());
    max_column_height_voxels = 0.0f;

    for (int z = 0; z < field.depth(); ++z)
    {
        for (int x = 0; x < field.width(); ++x)
        {
            const auto &cell = field.at(x, z);
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
            if (target == grannys_house_trials::sim::TargetId::GardenBedNorth && scenario.state().garden_bed_north_watered)
            {
                gpu_cell.soil_moisture = 0.95f;
                gpu_cell.fertility = std::max(gpu_cell.fertility, 0.94f);
            }
            else if (target == grannys_house_trials::sim::TargetId::CellarEdge && scenario.state().cellar_edge_saturated)
            {
                gpu_cell.material = static_cast<std::uint32_t>(grannys_house_trials::sim::TerrainMaterial::WetSoil);
                gpu_cell.soil_moisture = 0.98f;
            }
            else if (target == grannys_house_trials::sim::TargetId::FlatStoneRun && scenario.state().path_edge_softened)
            {
                gpu_cell.soil_moisture = 0.88f;
                gpu_cell.weed_pressure = std::max(gpu_cell.weed_pressure, 0.20f);
            }
            else if (target == grannys_house_trials::sim::TargetId::DrainMouth && scenario.state().drain_source_routed)
            {
                gpu_cell.soil_moisture = 1.00f;
            }

            cells.push_back(gpu_cell);
        }
    }

    return cells;
}

[[nodiscard]] std::vector<GpuFieldCell> build_hybrid_field_buffer_data(
    const grannys_house_trials::sim::GrassField &field,
    const grannys_house_trials::sim::GrannysYardScenario &scenario,
    const grannys_house_trials::sim::AdaptiveTerrainOwnershipField &ownership_field,
    float &max_column_height_voxels)
{
    std::vector<GpuFieldCell> cells;
    cells.reserve(field.cell_count());
    max_column_height_voxels = 0.0f;

    for (int coarse_z = 0; coarse_z < field.depth(); ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < field.width(); ++coarse_x)
        {
            const auto &coarse_cell = field.at(coarse_x, coarse_z);
            const int highest_non_empty_block_count = ownership_field.highest_non_empty_block_count_at(coarse_x, coarse_z);
            const int coarse_full_block_count = ownership_field.full_block_count_at(coarse_x, coarse_z);

            max_column_height_voxels = std::max(max_column_height_voxels, static_cast<float>(highest_non_empty_block_count));
            GpuFieldCell gpu_cell{
                .column_height_voxels = static_cast<float>(highest_non_empty_block_count),
                .material = static_cast<std::uint32_t>(coarse_cell.material),
                .is_homestead_pad = coarse_cell.is_homestead_pad ? 1u : 0u,
                .is_garden_bed = coarse_cell.garden.is_garden_bed ? 1u : 0u,
                .soil_moisture = coarse_cell.garden.soil_moisture,
                .fertility = coarse_cell.garden.fertility,
                .sunlight = coarse_cell.garden.sunlight,
                .weed_pressure = coarse_cell.garden.weed_pressure,
                .coarse_full_height_voxels = static_cast<float>(coarse_full_block_count),
            };

            const auto target = scenario_target_for_cell(coarse_x, coarse_z);
            if (target == grannys_house_trials::sim::TargetId::GardenBedNorth && scenario.state().garden_bed_north_watered)
            {
                gpu_cell.soil_moisture = 0.95f;
            }
            else if (target == grannys_house_trials::sim::TargetId::CellarEdge && scenario.state().cellar_edge_saturated)
            {
                gpu_cell.material = static_cast<std::uint32_t>(grannys_house_trials::sim::TerrainMaterial::WetSoil);
                gpu_cell.soil_moisture = 0.98f;
            }
            else if (target == grannys_house_trials::sim::TargetId::FlatStoneRun && scenario.state().path_edge_softened)
            {
                gpu_cell.soil_moisture = 0.88f;
            }
            else if (target == grannys_house_trials::sim::TargetId::DrainMouth && scenario.state().drain_source_routed)
            {
                gpu_cell.soil_moisture = 1.00f;
            }

            cells.push_back(gpu_cell);
        }
    }

    return cells;
}

struct SparseRefinedPatchGpuData
{
    std::vector<std::int32_t> lookup;
    std::vector<GpuRefinedPatchMetadata> metadata;
    std::vector<std::uint32_t> heights;
};

[[nodiscard]] SparseRefinedPatchGpuData build_sparse_refined_patch_gpu_data(
    const grannys_house_trials::sim::GrassField &field,
    const grannys_house_trials::sim::SparseRefinedPatchField &refined_patch_field)
{
    SparseRefinedPatchGpuData gpu_data{};
    gpu_data.lookup.assign(static_cast<std::size_t>(field.width() * field.depth()), -1);

    const int patch_resolution = refined_patch_field.patch_resolution();
    const std::size_t heights_per_patch =
        static_cast<std::size_t>(patch_resolution * patch_resolution);
    gpu_data.metadata.reserve(static_cast<std::size_t>(refined_patch_field.patch_count()));
    gpu_data.heights.reserve(static_cast<std::size_t>(refined_patch_field.patch_count()) * heights_per_patch);

    for (int patch_index = 0; patch_index < refined_patch_field.patch_count(); ++patch_index)
    {
        const auto &patch = refined_patch_field.patch_at_index(patch_index);
        const auto &coarse_cell = field.at(patch.coarse_x, patch.coarse_z);
        const std::uint32_t height_offset = static_cast<std::uint32_t>(gpu_data.heights.size());

        gpu_data.lookup[static_cast<std::size_t>(patch.coarse_z * field.width() + patch.coarse_x)] = patch_index;
        gpu_data.metadata.push_back(GpuRefinedPatchMetadata{
            .height_offset = height_offset,
            .material = static_cast<std::uint32_t>(coarse_cell.material),
            .is_homestead_pad = coarse_cell.is_homestead_pad ? 1u : 0u,
            .is_garden_bed = coarse_cell.garden.is_garden_bed ? 1u : 0u,
            .soil_moisture = coarse_cell.garden.soil_moisture,
            .fertility = coarse_cell.garden.fertility,
            .sunlight = coarse_cell.garden.sunlight,
            .weed_pressure = coarse_cell.garden.weed_pressure,
            .coarse_full_height_voxels = static_cast<float>(patch.coarse_full_height_inches),
            .patch_max_height_voxels = static_cast<float>(patch.max_height_inches),
        });

        for (std::int16_t top_height_inches : patch.top_heights_inches)
        {
            gpu_data.heights.push_back(static_cast<std::uint32_t>(top_height_inches));
        }
    }

    if (gpu_data.metadata.empty())
    {
        gpu_data.metadata.push_back(GpuRefinedPatchMetadata{});
    }

    if (gpu_data.heights.empty())
    {
        gpu_data.heights.push_back(0u);
    }

    return gpu_data;
}

[[nodiscard]] CameraMatrices build_camera_matrices(
    const grannys_house_trials::gfx::OrbitCamera &camera,
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

[[nodiscard]] ComPtr<ID3D12Resource> create_upload_buffer(
    ID3D12Device *device,
    UINT64 byte_size,
    const char *failure_message)
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
    const Ray &ray,
    const XMFLOAT3 &minimum,
    const XMFLOAT3 &maximum,
    float &distance)
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

class D3D12App
{
public:
    int run(HINSTANCE instance);

private:
    bool initialize(HINSTANCE instance);
    void initialize_pipeline();
    void on_viewport_resized(UINT width, UINT height);
    void create_assets();
    void update();
    void render();
    void update_fps_counter();
    void create_ui();
    void layout_ui();
    void refresh_info_panel();
    void refresh_evidence_board();
    [[nodiscard]] std::wstring build_agent_snapshot_text() const;
    void refresh_field_size_ui();
    void apply_field_size(int field_size);
    [[nodiscard]] std::optional<VoxelSelection> try_pick_voxel(POINT client_point) const;
    [[nodiscard]] std::optional<grannys_house_trials::sim::TargetId> selected_scenario_target() const;
    [[nodiscard]] std::vector<grannys_house_trials::sim::LegalAction> scenario_actions_for_selection() const;
    void refresh_scenario_action_ui();
    void run_selected_scenario_action();
    void run_scenario_action(std::string_view action_id);
    void apply_agent_json_from_clipboard();
    void rebuild_display_field_buffer();
    void ensure_field_buffer_capacity(UINT required_cells);
    void ensure_refined_patch_buffer_capacity(UINT lookup_count, UINT patch_count, UINT height_count);
    void set_display_grid_mode(DisplayGridMode mode);
    void on_mouse_down(MouseState::DragMode drag_mode, LPARAM lparam);
    void on_mouse_up(MouseState::DragMode drag_mode, LPARAM lparam);
    void on_mouse_move(WPARAM buttons, LPARAM lparam);
    void on_mouse_wheel(WPARAM wparam);
    void reset_camera();
    void step_erosion_cycle();
    [[nodiscard]] ID3D12PipelineState *active_pipeline_state() const noexcept;
    [[nodiscard]] int render_grid_width() const noexcept;
    [[nodiscard]] int render_grid_depth() const noexcept;
    [[nodiscard]] float render_voxel_size_feet() const noexcept;
    [[nodiscard]] int display_column_base_voxels_at(int display_x, int display_z) const;
    [[nodiscard]] int display_column_height_voxels_at(int display_x, int display_z) const;
    [[nodiscard]] int source_coarse_x_for_display_x(int display_x) const noexcept;
    [[nodiscard]] int source_coarse_z_for_display_z(int display_z) const noexcept;
    [[nodiscard]] int source_local_x_inches_for_display_x(int display_x) const noexcept;
    [[nodiscard]] int source_local_z_inches_for_display_z(int display_z) const noexcept;
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    HWND viewport_hwnd_ = nullptr;
    HWND title_label_ = nullptr;
    HWND hint_label_ = nullptr;
    HWND camera_group_ = nullptr;
    HWND selection_group_ = nullptr;
    HWND reset_camera_button_ = nullptr;
    HWND step_erosion_button_ = nullptr;
    HWND clear_selection_button_ = nullptr;
    HWND display_grid_label_ = nullptr;
    HWND display_grid_combo_ = nullptr;
    HWND field_size_label_ = nullptr;
    HWND field_size_combo_ = nullptr;
    HWND highlight_checkbox_ = nullptr;
    HWND fog_checkbox_ = nullptr;
    HWND packet_view_checkbox_ = nullptr;
    HWND copy_agent_snapshot_button_ = nullptr;
    HWND run_agent_json_button_ = nullptr;
    HWND scenario_group_ = nullptr;
    HWND scenario_action_combo_ = nullptr;
    HWND run_action_button_ = nullptr;
    HWND advance_round_button_ = nullptr;
    HWND end_round_button_ = nullptr;
    HWND reset_round_button_ = nullptr;
    HWND info_panel_ = nullptr;
    HWND evidence_board_group_ = nullptr;
    UINT width_ = 1280;
    UINT height_ = 720;
    MouseState mouse_{};
    int field_size_ = default_field_size;
    grannys_house_trials::gfx::OrbitCamera camera_{make_default_camera(default_field_size, default_field_size)};
    grannys_house_trials::sim::GrassField field_{default_field_size, default_field_size, default_voxel_size_feet};
    grannys_house_trials::sim::GravityErosionField erosion_field_{field_};
    grannys_house_trials::sim::AdaptiveTerrainOwnershipField ownership_field_{field_, erosion_field_};
    grannys_house_trials::sim::SparseRefinedPatchField refined_patch_field_{field_, erosion_field_, ownership_field_};
    grannys_house_trials::playtest::GrannysYardSession yard_session_{};
    DisplayGridMode display_grid_mode_ = DisplayGridMode::coarse_foot_columns;
    std::optional<VoxelSelection> selected_voxel_;
    bool highlight_selection_ = true;
    bool fog_enabled_ = true;
    bool packet_view_enabled_ = false;
    std::wstring latest_agent_snapshot_;
    grannys_house_trials::playtest::EvidenceBoardPanel evidence_board_panel_{};
    double fps_ = 0.0;
    std::chrono::steady_clock::time_point fps_sample_start_{};
    std::uint64_t fps_sample_frames_ = 0;
    HFONT ui_font_ = nullptr;
    HFONT info_font_ = nullptr;

    // Graphics rendering (D3D12 module)
    std::optional<grannys_house_trials::gfx::D3D12Context> context_;
    grannys_house_trials::gfx::UIFrameRenderer ui_frame_renderer_;

    ComPtr<ID3D12DescriptorHeap> cbv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> depth_stencil_;
    UINT cbv_srv_descriptor_size_ = 0;

    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> coarse_pipeline_state_;
    ComPtr<ID3D12PipelineState> refined_pipeline_state_;
    ComPtr<ID3D12PipelineState> hybrid_pipeline_state_;

    ComPtr<ID3D12Resource> field_buffer_;
    UINT field_buffer_capacity_cells_ = 0;
    UINT field_cell_count_ = 0;
    ComPtr<ID3D12Resource> refined_patch_lookup_buffer_;
    UINT refined_patch_lookup_capacity_ = 0;
    ComPtr<ID3D12Resource> refined_patch_metadata_buffer_;
    UINT refined_patch_capacity_ = 0;
    ComPtr<ID3D12Resource> refined_patch_height_buffer_;
    UINT refined_patch_height_capacity_ = 0;
    float field_origin_x_ = 0.0f;
    float field_origin_z_ = 0.0f;
    float display_voxel_size_feet_ = 1.0f;
    float max_column_height_voxels_ = 0.0f;

    ComPtr<ID3D12Resource> constant_buffer_;
    SceneConstants *constant_buffer_data_ = nullptr;
};

int D3D12App::run(HINSTANCE instance)
{
    debug_log("[grass-field-001] run begin");
    if (!initialize(instance))
    {
        debug_log("[grass-field-001] initialize returned false");
        return 1;
    }

    debug_log("[grass-field-001] initialize complete; showing main window");
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }

        update();
        render();
    }

    // D3D12Context destructor handles GPU synchronization and cleanup via RAII
    if (ui_font_)
    {
        DeleteObject(ui_font_);
        ui_font_ = nullptr;
    }
    if (info_font_)
    {
        DeleteObject(info_font_);
        info_font_ = nullptr;
    }
    return static_cast<int>(message.wParam);
}

bool D3D12App::initialize(HINSTANCE instance)
{
    debug_log("[grass-field-001] initialize begin");
    constexpr DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&common_controls);

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &D3D12App::window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = main_window_class_name;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);
    debug_log("[grass-field-001] main window class registered");

    WNDCLASSW viewport_class{};
    viewport_class.lpfnWndProc = &D3D12App::window_proc;
    viewport_class.hInstance = instance;
    viewport_class.lpszClassName = viewport_window_class_name;
    viewport_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&viewport_class);
    debug_log("[grass-field-001] viewport window class registered");

    constexpr LONG desired_client_width = 2200;
    constexpr LONG desired_client_height = 1840;
    RECT window_rect{0, 0, desired_client_width, desired_client_height};
    AdjustWindowRect(&window_rect, window_style, FALSE);

    hwnd_ = CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"Granny's House Trials :: Grass Field 001",
        window_style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_)
    {
        debug_log("[grass-field-001] CreateWindowExW for main window failed");
        return false;
    }

    debug_log("[grass-field-001] main window created");
    apply_dark_title_bar_if_available(hwnd_);
    debug_log("[grass-field-001] creating UI");
    create_ui();
    layout_ui();
    apply_dark_title_bar_if_available(viewport_hwnd_);
    debug_log("[grass-field-001] UI created and laid out");

    RECT viewport_client_rect{};
    GetClientRect(viewport_hwnd_, &viewport_client_rect);
    width_ = static_cast<UINT>(viewport_client_rect.right - viewport_client_rect.left);
    height_ = static_cast<UINT>(viewport_client_rect.bottom - viewport_client_rect.top);

    debug_log("[grass-field-001] initializing D3D12 pipeline");
    initialize_pipeline();
    debug_log("[grass-field-001] pipeline initialized");
    debug_log("[grass-field-001] creating assets");
    create_assets();
    debug_log("[grass-field-001] assets created");
    refresh_info_panel();
    debug_log("[grass-field-001] info panel refreshed");
    return true;
}

void D3D12App::initialize_pipeline()
{
    debug_log("[grass-field-001] initialize_pipeline begin");

    // Create D3D12Context (handles: factory, device, queue, swap chain, RTV heap, frame sync)
    context_.emplace(viewport_hwnd_, width_, height_, frame_count);
    debug_log("[grass-field-001] D3D12Context created");

    // Create CBV/SRV/UAV heap for field buffers and constants
    D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
    cbv_heap_desc.NumDescriptors = 4;
    cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    throw_if_failed(
        context_->device()->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap_)),
        "Could not create CBV heap.");
    cbv_srv_descriptor_size_ = context_->device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    debug_log("[grass-field-001] CBV/SRV/UAV heap created");

    // Create DSV heap for depth stencil
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    throw_if_failed(
        context_->device()->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_)),
        "Could not create DSV heap.");
    debug_log("[grass-field-001] DSV heap created");

    // Create depth stencil buffer
    D3D12_HEAP_PROPERTIES depth_heap{};
    depth_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depth_desc{};
    depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depth_desc.Width = width_;
    depth_desc.Height = height_;
    depth_desc.DepthOrArraySize = 1;
    depth_desc.MipLevels = 1;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depth_clear{};
    depth_clear.Format = DXGI_FORMAT_D32_FLOAT;
    depth_clear.DepthStencil.Depth = 1.0f;
    depth_clear.DepthStencil.Stencil = 0;

    throw_if_failed(
        context_->device()->CreateCommittedResource(
            &depth_heap,
            D3D12_HEAP_FLAG_NONE,
            &depth_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depth_clear,
            IID_PPV_ARGS(&depth_stencil_)),
        "Could not create depth buffer.");
    debug_log("[grass-field-001] Depth stencil resource created");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    context_->device()->CreateDepthStencilView(
        depth_stencil_.Get(),
        &dsv_desc,
        dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    debug_log("[grass-field-001] Depth stencil view created");

    // Create constant buffer for scene constants
    const UINT64 constant_buffer_size = (sizeof(SceneConstants) + 255) & ~255;
    constant_buffer_ = create_upload_buffer(
        context_->device(),
        constant_buffer_size,
        "Could not create constant buffer.");
    
    D3D12_RANGE read_range{0, 0};
    throw_if_failed(
        constant_buffer_->Map(0, &read_range, reinterpret_cast<void **>(&constant_buffer_data_)),
        "Could not map constant buffer.");
    debug_log("[grass-field-001] Constant buffer created and mapped");

    debug_log("[grass-field-001] initialize_pipeline complete");
}

void D3D12App::on_viewport_resized(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    // Keep local dimensions in sync with the viewport so UI/picking math remains consistent.
    width_ = width;
    height_ = height;

    if (context_ && context_->supports_resize())
    {
        context_->resize(width_, height_);
    }
}

void D3D12App::create_assets()
{
    debug_log("[grass-field-001] create_assets begin");
    const std::filesystem::path shader_directory = executable_directory() / L"shaders";
    debug_log("[grass-field-001] loading precompiled shader blobs");
    const ComPtr<ID3DBlob> vertex_shader = load_compiled_shader_blob(shader_directory / L"grass_field_vs.cso");
    const ComPtr<ID3DBlob> coarse_pixel_shader = load_compiled_shader_blob(shader_directory / L"grass_field_coarse_ps.cso");
    const ComPtr<ID3DBlob> refined_pixel_shader = load_compiled_shader_blob(shader_directory / L"grass_field_refined_ps.cso");
    const ComPtr<ID3DBlob> hybrid_pixel_shader = load_compiled_shader_blob(shader_directory / L"grass_field_hybrid_ps.cso");
    debug_log("[grass-field-001] shader blobs loaded");

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
        D3D12SerializeRootSignature(
            &root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &signature,
            &errors),
        "Could not serialize root signature.");
    throw_if_failed(
        context_->device()->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(&root_signature_)),
        "Could not create root signature.");

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    const auto create_pipeline_state = [&](ID3DBlob *pixel_shader,
                                           ComPtr<ID3D12PipelineState> &pipeline_state,
                                           std::string_view pipeline_name) {
        {
            std::ostringstream stream;
            stream << "[grass-field-001] creating pipeline state " << pipeline_name;
            debug_log(stream.str());
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.InputLayout = {nullptr, 0};
        pipeline_desc.pRootSignature = root_signature_.Get();
        pipeline_desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        pipeline_desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
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
        pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipeline_desc.SampleDesc.Count = 1;
        throw_if_failed(
            context_->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline_state)),
            "Could not create graphics pipeline.");
    };

    create_pipeline_state(coarse_pixel_shader.Get(), coarse_pipeline_state_, "coarse");
    create_pipeline_state(refined_pixel_shader.Get(), refined_pipeline_state_, "refined");
    create_pipeline_state(hybrid_pixel_shader.Get(), hybrid_pipeline_state_, "hybrid");
    debug_log("[grass-field-001] pipeline states created");

    field_buffer_capacity_cells_ = static_cast<UINT>(field_.cell_count());
    field_buffer_ = create_upload_buffer(
        context_->device(),
        static_cast<UINT64>(field_buffer_capacity_cells_) * sizeof(GpuFieldCell),
        "Could not create field buffer.");

    const UINT initial_lookup_count = static_cast<UINT>(field_.cell_count());
    const UINT initial_patch_count = std::max(1, refined_patch_field_.patch_count());
    const UINT initial_height_count = std::max(
        1u,
        initial_patch_count * static_cast<UINT>(refined_patch_field_.patch_resolution() * refined_patch_field_.patch_resolution()));

    refined_patch_lookup_capacity_ = initial_lookup_count;
    refined_patch_capacity_ = initial_patch_count;
    refined_patch_height_capacity_ = initial_height_count;

    refined_patch_lookup_buffer_ = create_upload_buffer(
        context_->device(),
        static_cast<UINT64>(refined_patch_lookup_capacity_) * sizeof(std::int32_t),
        "Could not create refined patch lookup buffer.");
    refined_patch_metadata_buffer_ = create_upload_buffer(
        context_->device(),
        static_cast<UINT64>(refined_patch_capacity_) * sizeof(GpuRefinedPatchMetadata),
        "Could not create refined patch metadata buffer.");
    refined_patch_height_buffer_ = create_upload_buffer(
        context_->device(),
        static_cast<UINT64>(refined_patch_height_capacity_) * sizeof(std::uint32_t),
        "Could not create refined patch height buffer.");

    debug_log("[grass-field-001] rebuilding initial display buffers");
    rebuild_display_field_buffer();
    debug_log("[grass-field-001] create_assets complete");
}

int D3D12App::render_grid_width() const noexcept
{
    return field_.width();
}

int D3D12App::render_grid_depth() const noexcept
{
    return field_.depth();
}

float D3D12App::render_voxel_size_feet() const noexcept
{
    return field_.voxel_size_feet();
}

int D3D12App::display_column_base_voxels_at(int display_x, int display_z) const
{
    if (display_grid_mode_ != DisplayGridMode::erosion_inch_columns)
    {
        return 0;
    }

    const int coarse_x = source_coarse_x_for_display_x(display_x);
    const int coarse_z = source_coarse_z_for_display_z(display_z);
    return ownership_field_.full_block_count_at(coarse_x, coarse_z) * erosion_field_.patch_resolution();
}

int D3D12App::display_column_height_voxels_at(int display_x, int display_z) const
{
    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        return field_.at(display_x, display_z).column_height_voxels;
    }

    const int coarse_x = source_coarse_x_for_display_x(display_x);
    const int coarse_z = source_coarse_z_for_display_z(display_z);
    const int local_x_inches = source_local_x_inches_for_display_x(display_x);
    const int local_z_inches = source_local_z_inches_for_display_z(display_z);
    return erosion_field_.fine_top_height_inches_at(coarse_x, coarse_z, local_x_inches, local_z_inches);
}

int D3D12App::source_coarse_x_for_display_x(int display_x) const noexcept
{
    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        return display_x;
    }

    return display_x / erosion_field_.patch_resolution();
}

int D3D12App::source_coarse_z_for_display_z(int display_z) const noexcept
{
    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        return display_z;
    }

    return display_z / erosion_field_.patch_resolution();
}

int D3D12App::source_local_x_inches_for_display_x(int display_x) const noexcept
{
    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        return erosion_field_.patch_resolution() / 2;
    }

    return display_x % erosion_field_.patch_resolution();
}

int D3D12App::source_local_z_inches_for_display_z(int display_z) const noexcept
{
    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        return erosion_field_.patch_resolution() / 2;
    }

    return display_z % erosion_field_.patch_resolution();
}

void D3D12App::ensure_field_buffer_capacity(UINT required_cells)
{
    if (required_cells <= field_buffer_capacity_cells_)
    {
        return;
    }

    field_buffer_ = create_upload_buffer(
        context_->device(),
        static_cast<UINT64>(required_cells) * sizeof(GpuFieldCell),
        "Could not grow field buffer.");
    field_buffer_capacity_cells_ = required_cells;
}

void D3D12App::ensure_refined_patch_buffer_capacity(UINT lookup_count, UINT patch_count, UINT height_count)
{
    if (lookup_count > refined_patch_lookup_capacity_)
    {
        refined_patch_lookup_buffer_ = create_upload_buffer(
            context_->device(),
            static_cast<UINT64>(lookup_count) * sizeof(std::int32_t),
            "Could not grow refined patch lookup buffer.");
        refined_patch_lookup_capacity_ = lookup_count;
    }

    if (patch_count > refined_patch_capacity_)
    {
        refined_patch_metadata_buffer_ = create_upload_buffer(
            context_->device(),
            static_cast<UINT64>(patch_count) * sizeof(GpuRefinedPatchMetadata),
            "Could not grow refined patch metadata buffer.");
        refined_patch_capacity_ = patch_count;
    }

    if (height_count > refined_patch_height_capacity_)
    {
        refined_patch_height_buffer_ = create_upload_buffer(
            context_->device(),
            static_cast<UINT64>(height_count) * sizeof(std::uint32_t),
            "Could not grow refined patch height buffer.");
        refined_patch_height_capacity_ = height_count;
    }
}

void D3D12App::rebuild_display_field_buffer()
{
    debug_log("[grass-field-001] rebuild_display_field_buffer begin");
    float max_column_height_voxels = 0.0f;
    std::vector<GpuFieldCell> display_cells;
    const SparseRefinedPatchGpuData refined_patch_gpu_data =
        build_sparse_refined_patch_gpu_data(field_, refined_patch_field_);

    if (display_grid_mode_ == DisplayGridMode::coarse_foot_columns)
    {
        display_cells = build_coarse_field_buffer_data(field_, yard_session_.scenario(), max_column_height_voxels);
    }
    else if (display_grid_mode_ == DisplayGridMode::erosion_inch_columns)
    {
        display_cells = build_coarse_field_buffer_data(field_, yard_session_.scenario(), max_column_height_voxels);
    }
    else
    {
        display_cells = build_hybrid_field_buffer_data(field_, yard_session_.scenario(), ownership_field_, max_column_height_voxels);
    }

    if (display_grid_mode_ != DisplayGridMode::coarse_foot_columns)
    {
        max_column_height_voxels = std::max(
            max_column_height_voxels,
            static_cast<float>(ownership_field_.max_non_empty_block_count()));
    }

    field_cell_count_ = static_cast<UINT>(display_cells.size());
    display_voxel_size_feet_ = render_voxel_size_feet();
    field_origin_x_ = -static_cast<float>(render_grid_width()) * display_voxel_size_feet_ * 0.5f;
    field_origin_z_ = -static_cast<float>(render_grid_depth()) * display_voxel_size_feet_ * 0.5f;
    max_column_height_voxels_ = max_column_height_voxels;

    if (context_)
    {
        context_->wait_for_gpu();
    }

    ensure_field_buffer_capacity(field_cell_count_);
    ensure_refined_patch_buffer_capacity(
        static_cast<UINT>(refined_patch_gpu_data.lookup.size()),
        static_cast<UINT>(refined_patch_gpu_data.metadata.size()),
        static_cast<UINT>(refined_patch_gpu_data.heights.size()));

    void *field_buffer_data = nullptr;
    D3D12_RANGE read_range{0, 0};
    throw_if_failed(
        field_buffer_->Map(0, &read_range, &field_buffer_data),
        "Could not map field buffer.");
    std::memcpy(
        field_buffer_data,
        display_cells.data(),
        static_cast<std::size_t>(field_cell_count_) * sizeof(GpuFieldCell));
    field_buffer_->Unmap(0, nullptr);

    void *lookup_buffer_data = nullptr;
    throw_if_failed(
        refined_patch_lookup_buffer_->Map(0, &read_range, &lookup_buffer_data),
        "Could not map refined patch lookup buffer.");
    std::memcpy(
        lookup_buffer_data,
        refined_patch_gpu_data.lookup.data(),
        refined_patch_gpu_data.lookup.size() * sizeof(std::int32_t));
    refined_patch_lookup_buffer_->Unmap(0, nullptr);

    void *metadata_buffer_data = nullptr;
    throw_if_failed(
        refined_patch_metadata_buffer_->Map(0, &read_range, &metadata_buffer_data),
        "Could not map refined patch metadata buffer.");
    std::memcpy(
        metadata_buffer_data,
        refined_patch_gpu_data.metadata.data(),
        refined_patch_gpu_data.metadata.size() * sizeof(GpuRefinedPatchMetadata));
    refined_patch_metadata_buffer_->Unmap(0, nullptr);

    void *height_buffer_data = nullptr;
    throw_if_failed(
        refined_patch_height_buffer_->Map(0, &read_range, &height_buffer_data),
        "Could not map refined patch height buffer.");
    std::memcpy(
        height_buffer_data,
        refined_patch_gpu_data.heights.data(),
        refined_patch_gpu_data.heights.size() * sizeof(std::uint32_t));
    refined_patch_height_buffer_->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = field_cell_count_;
    srv_desc.Buffer.StructureByteStride = sizeof(GpuFieldCell);
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = cbv_heap_->GetCPUDescriptorHandleForHeapStart();
    context_->device()->CreateShaderResourceView(field_buffer_.Get(), &srv_desc, descriptor);

    descriptor.ptr += cbv_srv_descriptor_size_;
    srv_desc.Buffer.NumElements = static_cast<UINT>(refined_patch_gpu_data.lookup.size());
    srv_desc.Buffer.StructureByteStride = sizeof(std::int32_t);
    context_->device()->CreateShaderResourceView(refined_patch_lookup_buffer_.Get(), &srv_desc, descriptor);

    descriptor.ptr += cbv_srv_descriptor_size_;
    srv_desc.Buffer.NumElements = static_cast<UINT>(refined_patch_gpu_data.metadata.size());
    srv_desc.Buffer.StructureByteStride = sizeof(GpuRefinedPatchMetadata);
    context_->device()->CreateShaderResourceView(refined_patch_metadata_buffer_.Get(), &srv_desc, descriptor);

    descriptor.ptr += cbv_srv_descriptor_size_;
    srv_desc.Buffer.NumElements = static_cast<UINT>(refined_patch_gpu_data.heights.size());
    srv_desc.Buffer.StructureByteStride = sizeof(std::uint32_t);
    context_->device()->CreateShaderResourceView(refined_patch_height_buffer_.Get(), &srv_desc, descriptor);
    debug_log("[grass-field-001] rebuild_display_field_buffer complete");
}

void D3D12App::set_display_grid_mode(DisplayGridMode mode)
{
    if (display_grid_mode_ == mode)
    {
        return;
    }

    display_grid_mode_ = mode;
    if (display_grid_combo_)
    {
        ComboBox_SetCurSel(display_grid_combo_, static_cast<int>(display_grid_mode_));
    }
    selected_voxel_.reset();
    rebuild_display_field_buffer();
    refresh_info_panel();
}

ID3D12PipelineState *D3D12App::active_pipeline_state() const noexcept
{
    switch (display_grid_mode_)
    {
    case DisplayGridMode::coarse_foot_columns:
        return coarse_pipeline_state_.Get();
    case DisplayGridMode::erosion_inch_columns:
        return refined_pipeline_state_.Get();
    case DisplayGridMode::hybrid_adaptive_columns:
        return hybrid_pipeline_state_.Get();
    }

    return coarse_pipeline_state_.Get();
}

void D3D12App::update()
{
    if (width_ == 0 || height_ == 0)
    {
        return;
    }

    const CameraMatrices matrices = build_camera_matrices(camera_, width_, height_);
    const XMMATRIX inverse_view_projection = XMMatrixTranspose(XMMatrixInverse(nullptr, matrices.view * matrices.projection));
    XMStoreFloat4x4(&constant_buffer_data_->inverse_view_projection, inverse_view_projection);
    constant_buffer_data_->camera_world_position = {
        matrices.eye_position.x,
        matrices.eye_position.y,
        matrices.eye_position.z,
        1.0f};
    constant_buffer_data_->field_origin_and_voxel_size = {
        field_origin_x_,
        field_origin_z_,
        display_voxel_size_feet_,
        max_column_height_voxels_};
    constant_buffer_data_->field_info = {
        static_cast<std::uint32_t>(render_grid_width()),
        static_cast<std::uint32_t>(render_grid_depth()),
        highlight_selection_ ? 1u : 0u,
        selected_voxel_ ? 1u : 0u};
    constant_buffer_data_->selection_info = {
        selected_voxel_ ? static_cast<std::uint32_t>(selected_voxel_->x) : 0u,
        selected_voxel_ ? static_cast<std::uint32_t>(selected_voxel_->y) : 0u,
        selected_voxel_ ? static_cast<std::uint32_t>(selected_voxel_->z) : 0u,
        static_cast<std::uint32_t>(display_grid_mode_)};
    constant_buffer_data_->refinement_info = {
        static_cast<std::uint32_t>(erosion_field_.patch_resolution()),
        static_cast<std::uint32_t>(refined_patch_field_.patch_count()),
        fog_enabled_ ? 1u : 0u,
        0u};
    constant_buffer_data_->display_info = {
        fog_enabled_ ? 1u : 0u,
        0u,
        0u,
        0u};

    update_fps_counter();
}

void D3D12App::update_fps_counter()
{
    const auto now = std::chrono::steady_clock::now();
    if (fps_sample_start_ == std::chrono::steady_clock::time_point{})
    {
        fps_sample_start_ = now;
        fps_sample_frames_ = 0;
        fps_ = 0.0;
        return;
    }

    ++fps_sample_frames_;
    const auto elapsed_seconds = std::chrono::duration<double>(now - fps_sample_start_).count();
    if (elapsed_seconds < 0.5)
    {
        return;
    }

    fps_ = elapsed_seconds > 0.0 ? static_cast<double>(fps_sample_frames_) / elapsed_seconds : 0.0;
    fps_sample_start_ = now;
    fps_sample_frames_ = 0;
}

void D3D12App::create_ui()
{
    const HINSTANCE app_instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwnd_, GWLP_HINSTANCE));

    title_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Grass Field 001",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    hint_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Host shell around the D3D viewport.\r\nUse the controls to run the drainage test, inspect a target, and read the state log.",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    camera_group_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"View Controls",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    reset_camera_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Reset Camera",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_reset_camera)),
        app_instance,
        nullptr);

    step_erosion_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Step Erosion",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_step_erosion)),
        app_instance,
        nullptr);

    clear_selection_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Clear Selection",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_clear_selection)),
        app_instance,
        nullptr);

    display_grid_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Display Grid",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    display_grid_combo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_display_grid_combo)),
        app_instance,
        nullptr);

    field_size_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Field Size",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    field_size_combo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_field_size_combo)),
        app_instance,
        nullptr);

    highlight_checkbox_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Highlight Selected Voxel",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_toggle_highlight)),
        app_instance,
        nullptr);
    Button_SetCheck(highlight_checkbox_, highlight_selection_ ? BST_CHECKED : BST_UNCHECKED);

    fog_checkbox_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Fog Enabled",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_toggle_fog)),
        app_instance,
        nullptr);
    Button_SetCheck(fog_checkbox_, fog_enabled_ ? BST_CHECKED : BST_UNCHECKED);

    packet_view_checkbox_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Agent Packet View",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_toggle_packet_view)),
        app_instance,
        nullptr);
    Button_SetCheck(packet_view_checkbox_, packet_view_enabled_ ? BST_CHECKED : BST_UNCHECKED);

    copy_agent_snapshot_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Copy Agent JSON",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_copy_agent_snapshot)),
        app_instance,
        nullptr);

    run_agent_json_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Apply Agent JSON",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_run_agent_json)),
        app_instance,
        nullptr);

    scenario_group_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Round Controls",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    scenario_action_combo_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_scenario_action_combo)),
        app_instance,
        nullptr);

    run_action_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Run Target Action",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_run_action)),
        app_instance,
        nullptr);

    advance_round_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Advance Round",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_advance_round)),
        app_instance,
        nullptr);

    end_round_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"End Round",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_end_round)),
        app_instance,
        nullptr);

    reset_round_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Reset Round",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id_reset_round)),
        app_instance,
        nullptr);

    evidence_board_group_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Evidence Board",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    selection_group_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Selected Target",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    info_panel_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        nullptr);

    evidence_board_panel_.create(hwnd_, app_instance);

    viewport_hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        viewport_window_class_name,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_instance,
        this);

    if (!title_label_ || !hint_label_ || !camera_group_ || !reset_camera_button_
        || !step_erosion_button_ || !display_grid_label_ || !display_grid_combo_
        || !field_size_label_ || !field_size_combo_
        || !clear_selection_button_ || !highlight_checkbox_ || !fog_checkbox_ || !packet_view_checkbox_ || !copy_agent_snapshot_button_ || !run_agent_json_button_
        || !scenario_group_ || !scenario_action_combo_ || !run_action_button_
        || !advance_round_button_ || !end_round_button_ || !reset_round_button_
        || !selection_group_ || !info_panel_ || !evidence_board_group_ || !evidence_board_panel_.hwnd() || !viewport_hwnd_)
    {
        throw std::runtime_error("Could not create the host application controls.");
    }

    ui_font_ = CreateFontW(
        -18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");

    info_font_ = CreateFontW(
        -17,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas");

    const HWND themed_controls[] = {
        reset_camera_button_,
        step_erosion_button_,
        clear_selection_button_,
        display_grid_combo_,
        scenario_action_combo_,
        highlight_checkbox_,
        fog_checkbox_,
        packet_view_checkbox_,
        copy_agent_snapshot_button_,
        run_agent_json_button_,
        run_action_button_,
        advance_round_button_,
        end_round_button_,
        reset_round_button_,
        info_panel_,
        evidence_board_group_,
    };

    for (HWND control : themed_controls)
    {
        apply_explorer_theme(control);
    }

    if (ui_font_)
    {
        const HWND ui_controls[] = {
            title_label_,
            hint_label_,
            camera_group_,
            scenario_group_,
            evidence_board_group_,
            selection_group_,
            reset_camera_button_,
            step_erosion_button_,
            clear_selection_button_,
            display_grid_label_,
            display_grid_combo_,
            field_size_label_,
            field_size_combo_,
            scenario_action_combo_,
            highlight_checkbox_,
            fog_checkbox_,
            packet_view_checkbox_,
            copy_agent_snapshot_button_,
            run_agent_json_button_,
            run_action_button_,
            advance_round_button_,
            end_round_button_,
            reset_round_button_,
            evidence_board_panel_.hwnd(),
        };

        for (HWND control : ui_controls)
        {
            SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
        }
    }

    if (info_font_)
    {
        SendMessage(info_panel_, WM_SETFONT, reinterpret_cast<WPARAM>(info_font_), TRUE);
    }

    if (ui_font_)
    {
        SendMessage(evidence_board_group_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    }

    Button_SetCheck(highlight_checkbox_, BST_CHECKED);
    ComboBox_AddString(display_grid_combo_, L"Coarse 1-foot");
    ComboBox_AddString(display_grid_combo_, L"Refined 1-inch");
    ComboBox_AddString(display_grid_combo_, L"Hybrid adaptive");
    ComboBox_SetCurSel(display_grid_combo_, static_cast<int>(display_grid_mode_));
    ComboBox_AddString(field_size_combo_, L"Small test field (50 x 50)");
    ComboBox_AddString(field_size_combo_, L"Standard test field (100 x 100)");
    ComboBox_AddString(field_size_combo_, L"Larger test field (150 x 150)");
    ComboBox_AddString(field_size_combo_, L"Stress test field (200 x 200)");
    ComboBox_AddString(field_size_combo_, L"Large garden test (1000 x 1000)");
    ComboBox_SetCurSel(field_size_combo_, 1);
    EnableWindow(copy_agent_snapshot_button_, FALSE);
    EnableWindow(run_agent_json_button_, TRUE);
    EnableWindow(run_action_button_, FALSE);
    EnableWindow(advance_round_button_, TRUE);
    EnableWindow(reset_round_button_, TRUE);
    refresh_scenario_action_ui();
    refresh_field_size_ui();
    layout_ui();
}

void D3D12App::layout_ui()
{
    if (!viewport_hwnd_ || !info_panel_)
    {
        return;
    }

    RECT client_rect{};
    GetClientRect(hwnd_, &client_rect);

    const int margin = 16;
    const int sidebar_width = 460;
    const int content_gap = 20;
    const int button_height = 34;
    const int button_width = 190;
    const int viewport_x = sidebar_width + content_gap;
    const int viewport_width = std::max<int>(320, static_cast<int>(client_rect.right) - viewport_x - margin);
    const int viewport_height = std::max<int>(320, static_cast<int>(client_rect.bottom) - (margin * 2));

    MoveWindow(title_label_, margin, margin, sidebar_width - (margin * 2), 26, TRUE);
    MoveWindow(hint_label_, margin, 48, sidebar_width - (margin * 2), 68, TRUE);

    MoveWindow(camera_group_, margin, 128, sidebar_width - (margin * 2), 196, TRUE);
    MoveWindow(reset_camera_button_, margin + 16, 160, button_width, button_height, TRUE);
    MoveWindow(step_erosion_button_, margin + 232, 160, button_width, button_height, TRUE);
    MoveWindow(clear_selection_button_, margin + 16, 206, button_width, button_height, TRUE);
    MoveWindow(display_grid_label_, margin + 232, 206, button_width, 20, TRUE);
    MoveWindow(display_grid_combo_, margin + 232, 228, button_width, 240, TRUE);
    MoveWindow(field_size_label_, margin + 16, 248, 90, 20, TRUE);
    MoveWindow(field_size_combo_, margin + 116, 244, button_width, 240, TRUE);
    MoveWindow(highlight_checkbox_, margin + 16, 292, sidebar_width - 48, 24, TRUE);
    MoveWindow(fog_checkbox_, margin + 16, 316, sidebar_width - 48, 24, TRUE);
    MoveWindow(packet_view_checkbox_, margin + 16, 340, sidebar_width - 48, 24, TRUE);

    MoveWindow(scenario_group_, margin, 372, sidebar_width - (margin * 2), 164, TRUE);
    MoveWindow(scenario_action_combo_, margin + 16, 396, sidebar_width - 48, 240, TRUE);
    MoveWindow(run_action_button_, margin + 16, 432, button_width, button_height, TRUE);
    MoveWindow(advance_round_button_, margin + 252, 432, button_width, button_height, TRUE);
    MoveWindow(end_round_button_, margin + 16, 474, button_width, button_height, TRUE);
    MoveWindow(reset_round_button_, margin + 252, 474, button_width, button_height, TRUE);

    MoveWindow(selection_group_, margin, 546, sidebar_width - (margin * 2), 996, TRUE);
    MoveWindow(copy_agent_snapshot_button_, margin + 16, 570, sidebar_width - 48, button_height, TRUE);
    MoveWindow(run_agent_json_button_, margin + 16, 612, sidebar_width - 48, button_height, TRUE);
    const int info_panel_y = 654;
    const int info_panel_height = 886;
    MoveWindow(info_panel_, margin + 16, info_panel_y, sidebar_width - 48, info_panel_height, TRUE);
    const int evidence_board_group_y = 1840 - 168 - 100;
    const int evidence_board_panel_y = evidence_board_group_y + 24;
    MoveWindow(evidence_board_group_, margin, evidence_board_group_y, sidebar_width - (margin * 2), 168, TRUE);
    evidence_board_panel_.layout(margin + 16, evidence_board_panel_y, sidebar_width - 48, 136);

    MoveWindow(viewport_hwnd_, viewport_x, margin, viewport_width, viewport_height, TRUE);
}

std::wstring D3D12App::build_agent_snapshot_text() const
{
    const auto presentation = yard_session_.round_presentation(selected_scenario_target());
    const auto &turn_packet = presentation.packet;
    const auto scenario_target = turn_packet.focused_target;
    const auto &evidence_view = presentation.evidence_board;
    const auto actions = turn_packet.legal_actions;

    std::wostringstream json;
    json << L"{\n"
         << L"  \"schema\": \"grannys_house_trials.grass_field.agent_snapshot.v4\",\n"
         << L"  \"mission\": \"Drain the yard without flooding the cellar edge or softening the path.\",\n"
         << L"  \"display_grid\": \"" << widen(display_grid_mode_name(display_grid_mode_)) << L"\",\n"
         << L"  \"field_size\": { \"width\": " << field_.width() << L", \"depth\": " << field_.depth() << L" },\n"
         << L"  \"selection\": ";

    if (!selected_voxel_)
    {
        json << L"null";
    }
    else
    {
        const auto &selection = *selected_voxel_;
        const int coarse_x = source_coarse_x_for_display_x(selection.x);
        const int coarse_z = source_coarse_z_for_display_z(selection.z);
        const auto &cell = field_.at(coarse_x, coarse_z);
        const int patch_resolution = erosion_field_.patch_resolution();
        const int selected_block_y =
            display_grid_mode_ == DisplayGridMode::coarse_foot_columns
                ? selection.y
                : selection.y / patch_resolution;
        const auto selected_block_ownership = ownership_field_.ownership_at(coarse_x, coarse_z, selected_block_y);

        json << L"{\n"
             << L"    \"x\": " << selection.x << L",\n"
             << L"    \"y\": " << selection.y << L",\n"
             << L"    \"z\": " << selection.z << L",\n"
             << L"    \"source_coarse\": { \"x\": " << coarse_x << L", \"z\": " << coarse_z << L" },\n"
             << L"    \"material\": \"" << widen(escape_json_string(grannys_house_trials::sim::terrain_material_name(cell.material))) << L"\",\n"
             << L"    \"column_height_voxels\": " << cell.column_height_voxels << L",\n"
             << L"    \"garden_bed\": " << (cell.garden.is_garden_bed ? L"true" : L"false") << L",\n"
             << L"    \"homestead_pad\": " << (cell.is_homestead_pad ? L"true" : L"false") << L",\n"
             << L"    \"ownership\": \"" << widen(terrain_volume_ownership_name(selected_block_ownership)) << L"\"\n"
             << L"  }";
    }

    json << L",\n"
         << L"  \"objective\": \"" << widen(escape_json_string(turn_packet.objective.view())) << L"\",\n"
         << L"  \"focused_target\": ";

    if (scenario_target)
    {
        json << L"\"" << widen(escape_json_string(grannys_house_trials::sim::to_string(*scenario_target))) << L"\"";
    }
    else
    {
        json << L"null";
    }

    json << L",\n"
         << L"  \"round_status\": \"" << widen(std::string(grannys_house_trials::playtest::round_result_name(turn_packet.round_result))) << L"\",\n"
         << L"  \"hidden_dependency_revealed\": " << (turn_packet.hidden_dependency_revealed ? L"true" : L"false") << L",\n"
         << L"  \"legal_actions\": [";

    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        if (index > 0)
        {
            json << L", ";
        }
        json << L"\"" << widen(escape_json_string(actions[index].id.str())) << L"\"";
    }

    json << L"],\n"
         << L"  \"recommended_actions\": [";

    const auto &recommended_actions = turn_packet.recommended_actions;
    for (std::size_t index = 0; index < recommended_actions.size(); ++index)
    {
        if (index > 0)
        {
            json << L", ";
        }
        json << L"{\"action_id\":\""
             << widen(escape_json_string(recommended_actions[index].action_id.str()))
             << L"\"";
        if (recommended_actions[index].target.has_value())
        {
            json << L",\"target\":\""
                 << widen(escape_json_string(grannys_house_trials::sim::to_string(*recommended_actions[index].target)))
                 << L"\"";
        }
        json << L"}";
    }

    json << L"],\n"
         << L"  \"evidence\": {\n";

    for (std::size_t index = 0; index < evidence_view.stats.size(); ++index)
    {
        const auto &stat = evidence_view.stats[index];
        json << L"    \"" << widen(grannys_house_trials::sim::to_string(stat.type)) << L"\": " << stat.count;
        if (index + 1 < evidence_view.stats.size())
        {
            json << L",";
        }
        json << L"\n";
    }

    json << L"  },\n"
         << L"  \"recent_events\": [";

    const auto &events = turn_packet.recent_events;
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        if (index > 0)
        {
            json << L", ";
        }
        json << L"\"" << widen(escape_json_string(events[index])) << L"\"";
    }

    json << L"],\n"
         << L"  \"commands\": [\n"
         << L"    \"Inspect voxel\",\n"
         << L"    \"Run selected action\",\n"
         << L"    \"Advance round\",\n"
         << L"    \"End round\",\n"
         << L"    \"Reset round\",\n"
         << L"    \"Change display grid\",\n"
         << L"    \"Change field size\",\n"
         << L"    \"Copy agent JSON\",\n"
         << L"    \"Apply Agent JSON\"\n"
         << L"  ]\n"
         << L"}\n";

    return json.str();
}

void D3D12App::refresh_info_panel()
{
    const auto presentation = yard_session_.round_presentation(selected_scenario_target());
    const auto &turn_packet = presentation.packet;
    const auto &evidence_view = presentation.evidence_board;
    evidence_board_panel_.set_view(evidence_view);
    const auto scenario_target = turn_packet.focused_target;
    const bool gameplay_actions_enabled = presentation.gameplay_actions_enabled;
    const auto scenario_actions = gameplay_actions_enabled ? scenario_actions_for_selection() : std::vector<grannys_house_trials::sim::LegalAction>{};
    const auto &token_events = turn_packet.recent_events;
    const auto &human_notes = turn_packet.human_notes;
    latest_agent_snapshot_ = build_agent_snapshot_text();
    const std::size_t packet_characters = latest_agent_snapshot_.size();
    const std::size_t packet_bytes = packet_characters * sizeof(wchar_t);

    std::wostringstream text;
    text << L"Grass Field 001\r\n"
         << L"Mission: route water to the garden beds without flooding the cellar edge or softening the path.\r\n"
         << L"Round status: " << widen(std::string(grannys_house_trials::playtest::round_result_name(turn_packet.round_result))) << L"\r\n"
         << L"FPS: " << std::fixed << std::setprecision(1) << fps_ << L"\r\n"
         << L"View: " << widen(display_grid_mode_name(display_grid_mode_))
         << L" | Erosion cycles: " << erosion_field_.cycle_count()
         << L" | Fog: " << (fog_enabled_ ? L"on" : L"off") << L"\r\n"
         << L"Packet view: " << (packet_view_enabled_ ? L"compact agent packet" : L"human summary") << L"\r\n";

    if (!selected_voxel_)
    {
        latest_agent_snapshot_.clear();
        if (packet_view_enabled_)
        {
            text << L"Selection: none\r\n"
                 << L"Target: none\r\n"
                 << L"Current state: " << (gameplay_actions_enabled ? L"waiting for a target or action" : L"round ended") << L"\r\n";
            if (gameplay_actions_enabled)
            {
                text << L"Possible actions: click the 3D view to pick a target\r\n"
                     << L"  - End Round to freeze the evidence board\r\n";
            }
            else
            {
                text << L"Round summary:\r\n"
                     << L"  - the round has ended; reset to begin another attempt\r\n";
            }
            text << L"Packet size: 0 characters / 0 bytes\r\n"
                 << L"Clipboard payload: compact agent packet is empty until a target is selected\r\n";
        }
        else
        {
            text << L"Selection: none\r\n"
                 << L"Target: none\r\n"
                 << L"Current state: " << (gameplay_actions_enabled ? L"waiting for a target or action" : L"round ended") << L"\r\n";
            if (gameplay_actions_enabled)
            {
                text << L"Possible actions:\r\n"
                     << L"  - Click the 3D view to pick a target\r\n"
                     << L"  - Run Target Action to act on the selected target\r\n"
                     << L"  - Advance Round to settle drainage one step\r\n"
                     << L"  - End Round to freeze the evidence board\r\n"
                     << L"  - Reset Round to restore the yard\r\n"
                     << L"  - Display Grid to switch coarse / refined / hybrid\r\n"
                     << L"  - Field Size to rebuild the square test field\r\n"
                     << L"  - Copy Agent JSON to copy the compact agent snapshot\r\n"
                     << L"  - Apply Agent JSON to run a clipboard command\r\n";
            }
            else
            {
                text << L"Round summary:\r\n"
                     << L"  - the round has ended; reset to begin another attempt\r\n";
            }
            text << L"\r\nState log:\r\n";
            if (human_notes.empty())
            {
                text << L"  - no recent notes yet\r\n";
            }
            else
            {
                for (const auto &note : human_notes)
                {
                    text << L"  - " << widen(note) << L"\r\n";
                }
            }
        }

        SetWindowTextW(info_panel_, text.str().c_str());
        std::wstring title = L"Granny's House Trials :: Grass Field 001 :: ";
        title += widen(display_grid_mode_name(display_grid_mode_));
        SetWindowTextW(hwnd_, title.c_str());
        EnableWindow(copy_agent_snapshot_button_, FALSE);
        EnableWindow(end_round_button_, gameplay_actions_enabled ? TRUE : FALSE);
        EnableWindow(reset_round_button_, TRUE);
        return;
    }

    const auto &selection = *selected_voxel_;
    const int coarse_x = source_coarse_x_for_display_x(selection.x);
    const int coarse_z = source_coarse_z_for_display_z(selection.z);
    const auto &cell = field_.at(coarse_x, coarse_z);
    const std::wstring material_name = widen(grannys_house_trials::sim::terrain_material_name(cell.material));
    const bool has_detail_patch = field_.has_detail_patch(coarse_x, coarse_z);
    const int coarse_top_height_inches = field_.coarse_top_height_inches_at(coarse_x, coarse_z);
    const int selected_fine_height = erosion_field_.fine_top_height_inches_at(
        coarse_x,
        coarse_z,
        source_local_x_inches_for_display_x(selection.x),
        source_local_z_inches_for_display_z(selection.z));
    const int patch_resolution = erosion_field_.patch_resolution();
    const int selected_block_y =
        display_grid_mode_ == DisplayGridMode::coarse_foot_columns
            ? selection.y
            : selection.y / patch_resolution;
    const auto selected_block_ownership = ownership_field_.ownership_at(coarse_x, coarse_z, selected_block_y);
    if (packet_view_enabled_)
    {
        text << L"Selection: (" << selection.x << L", " << selection.y << L", " << selection.z << L")\r\n"
             << L"Target: " << (scenario_target ? widen(grannys_house_trials::sim::to_string(*scenario_target)) : L"none") << L"\r\n"
             << L"Round status: " << widen(std::string(grannys_house_trials::playtest::round_result_name(turn_packet.round_result))) << L"\r\n"
             << L"Packet size: " << packet_characters << L" characters / " << packet_bytes << L" bytes\r\n"
             << L"Legal actions: " << turn_packet.legal_actions.size() << L"\r\n"
             << L"Recent events: " << token_events.size() << L"\r\n"
             << L"Evidence items: " << evidence_view.stats.size() << L"\r\n"
             << L"\r\nAgent packet:\r\n"
             << latest_agent_snapshot_;
    }
    else
    {
        text << L"Selection: (" << selection.x << L", " << selection.y << L", " << selection.z << L")\r\n"
             << L"Target: " << (scenario_target ? widen(grannys_house_trials::sim::to_string(*scenario_target)) : L"none") << L"\r\n"
             << L"Current state: " << widen(std::string(grannys_house_trials::playtest::round_result_name(turn_packet.round_result))) << L"\r\n"
             << L"Hidden dependency: " << (turn_packet.hidden_dependency_revealed ? L"revealed" : L"still hidden") << L"\r\n"
             << L"Material: " << material_name << L"\r\n"
             << L"Height: " << cell.column_height_voxels << L" coarse voxels, top at " << coarse_top_height_inches << L" inches\r\n"
             << L"Ownership: " << widen(terrain_volume_ownership_name(selected_block_ownership)) << L"\r\n"
             << L"Detail patch: " << (has_detail_patch ? L"yes" : L"no") << L"\r\n"
             << L"Settled top: " << selected_fine_height << L" inches\r\n"
             << L"Packet size: " << packet_characters << L" characters / " << packet_bytes << L" bytes\r\n"
             << L"Legal actions: " << scenario_actions.size() << L"\r\n"
             << L"\r\nPossible actions:\r\n";

        if (!gameplay_actions_enabled)
        {
            text << L"  round ended; reset to start a new attempt\r\n";
        }
        else if (scenario_actions.empty())
        {
            text << L"  none\r\n";
        }
        else
        {
            for (const auto &action : scenario_actions)
            {
                text << L"  - " << legal_action_label(action) << L"\r\n";
            }
        }

        text << L"\r\nState log:\r\n";
        if (human_notes.empty())
        {
            text << L"  - no recent events yet\r\n";
        }
        else
        {
            for (const std::string &note : human_notes)
            {
                text << L"  - " << widen(note) << L"\r\n";
            }
        }
    }

    SetWindowTextW(info_panel_, text.str().c_str());
    EnableWindow(copy_agent_snapshot_button_, true);
    EnableWindow(end_round_button_, gameplay_actions_enabled ? TRUE : FALSE);
    EnableWindow(reset_round_button_, TRUE);

    std::wostringstream title;
    title << L"Granny's House Trials :: Grass Field 001 :: "
          << (packet_view_enabled_ ? L"agent packet view" : L"human summary")
          << L" :: erosion " << erosion_field_.cycle_count();
    SetWindowTextW(hwnd_, title.str().c_str());
}

void D3D12App::refresh_evidence_board()
{
    evidence_board_panel_.set_view(yard_session_.round_presentation(selected_scenario_target()).evidence_board);
}

std::optional<grannys_house_trials::sim::TargetId> D3D12App::selected_scenario_target() const
{
    if (!selected_voxel_)
    {
        return std::nullopt;
    }

    return scenario_target_for_cell(
        source_coarse_x_for_display_x(selected_voxel_->x),
        source_coarse_z_for_display_z(selected_voxel_->z));
}

std::vector<grannys_house_trials::sim::LegalAction> D3D12App::scenario_actions_for_selection() const
{
    return yard_session_.legal_actions(selected_scenario_target());
}

void D3D12App::refresh_scenario_action_ui()
{
    if (!scenario_action_combo_ || !run_action_button_)
    {
        return;
    }

    ComboBox_ResetContent(scenario_action_combo_);
    const auto actions = yard_session_.can_accept_gameplay_actions() ? scenario_actions_for_selection() : std::vector<grannys_house_trials::sim::LegalAction>{};
    for (const auto &action : actions)
    {
        const std::wstring item_text = legal_action_label(action) + L" [" + widen(action.id.str()) + L"]";
        ComboBox_AddString(scenario_action_combo_, item_text.c_str());
    }

    const bool has_actions = !actions.empty();
    ComboBox_SetCurSel(scenario_action_combo_, has_actions ? 0 : -1);
    EnableWindow(scenario_action_combo_, has_actions);
    EnableWindow(run_action_button_, has_actions);
}

void D3D12App::refresh_field_size_ui()
{
    if (!field_size_combo_)
    {
        return;
    }

    const int current_size = field_.width();
    const int current_index = current_size <= 50 ? 0 : current_size <= 100 ? 1 : current_size <= 150 ? 2 : current_size <= 200 ? 3 : 4;
    ComboBox_SetCurSel(field_size_combo_, current_index);
}

void D3D12App::apply_field_size(int field_size)
{
    field_size = std::clamp(field_size, 50, 1000);
    if (field_size == field_.width() && field_size == field_.depth())
    {
        return;
    }

    field_size_ = field_size;
    field_ = grannys_house_trials::sim::GrassField(field_size_, field_size_, default_voxel_size_feet);
    erosion_field_ = grannys_house_trials::sim::GravityErosionField(field_);
    ownership_field_ = grannys_house_trials::sim::AdaptiveTerrainOwnershipField(field_, erosion_field_);
    refined_patch_field_ = grannys_house_trials::sim::SparseRefinedPatchField(field_, erosion_field_, ownership_field_);
    camera_ = make_default_camera(field_size_, field_size_);
    selected_voxel_.reset();
    static_cast<void>(yard_session_.reset_round());
    rebuild_display_field_buffer();
    refresh_field_size_ui();
    refresh_scenario_action_ui();
    refresh_info_panel();
}

void D3D12App::run_selected_scenario_action()
{
    const int selection_index = ComboBox_GetCurSel(scenario_action_combo_);
    if (selection_index < 0)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const auto actions = scenario_actions_for_selection();
    if (selection_index >= static_cast<int>(actions.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    run_scenario_action(actions[static_cast<std::size_t>(selection_index)].id.view());
}

void D3D12App::run_scenario_action(std::string_view action_id)
{
    if (action_id == "end_round")
    {
        static_cast<void>(yard_session_.end_round(selected_scenario_target()));
    }
    else if (action_id == "reset_round")
    {
        static_cast<void>(yard_session_.reset_round());
    }
    else if (action_id == "advance_simulation")
    {
        static_cast<void>(yard_session_.advance_round(selected_scenario_target()));
    }
    else
    {
        static_cast<void>(yard_session_.run_action(action_id, selected_scenario_target()));
    }

    rebuild_display_field_buffer();
    refresh_scenario_action_ui();
    refresh_info_panel();
}

void D3D12App::apply_agent_json_from_clipboard()
{
    const auto maybe_clipboard = read_text_from_clipboard(hwnd_);
    if (!maybe_clipboard)
    {
        MessageBoxW(hwnd_, L"Clipboard does not contain text.", L"Agent JSON", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto utf8_payload = narrow_from_wide(*maybe_clipboard);
    if (!utf8_payload)
    {
        MessageBoxW(hwnd_, L"Could not convert clipboard text to UTF-8.", L"Agent JSON", MB_OK | MB_ICONERROR);
        return;
    }

    std::string parse_error;
    const auto command = grannys_house_trials::playtest::parse_agent_command(*utf8_payload, parse_error);
    if (!command)
    {
        std::wstring message = L"Failed to parse agent command:\r\n";
        message += widen(parse_error);
        MessageBoxW(hwnd_, message.c_str(), L"Agent JSON", MB_OK | MB_ICONERROR);
        return;
    }

    if (command->selection)
    {
        const int selection_x = command->selection->x;
        const int selection_y = command->selection->y;
        const int selection_z = command->selection->z;

        if (selection_x < 0 || selection_x >= render_grid_width()
            || selection_z < 0 || selection_z >= render_grid_depth())
        {
            MessageBoxW(hwnd_, L"Selection coordinates are outside the current display grid.", L"Agent JSON", MB_OK | MB_ICONERROR);
            return;
        }

        const int column_base = display_column_base_voxels_at(selection_x, selection_z);
        const int column_top = display_column_height_voxels_at(selection_x, selection_z);
        if (column_top <= column_base)
        {
            MessageBoxW(hwnd_, L"Selection coordinates point to an empty column.", L"Agent JSON", MB_OK | MB_ICONERROR);
            return;
        }

        if (selection_y < column_base || selection_y >= column_top)
        {
            MessageBoxW(hwnd_, L"selection_y is outside the selected column height range.", L"Agent JSON", MB_OK | MB_ICONERROR);
            return;
        }

        selected_voxel_ = VoxelSelection{selection_x, selection_y, selection_z};
    }

    auto selection_for_target = [this](grannys_house_trials::sim::TargetId target) -> std::optional<VoxelSelection> {
        const int grid_width = render_grid_width();
        const int grid_depth = render_grid_depth();

        int min_x = std::numeric_limits<int>::max();
        int max_x = std::numeric_limits<int>::min();
        int min_z = std::numeric_limits<int>::max();
        int max_z = std::numeric_limits<int>::min();
        bool found = false;

        for (int z = 0; z < grid_depth; ++z)
        {
            for (int x = 0; x < grid_width; ++x)
            {
                const auto cell_target = scenario_target_for_cell(x, z);
                if (!cell_target || *cell_target != target)
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
        const int column_base = display_column_base_voxels_at(selection_x, selection_z);
        const int column_top = display_column_height_voxels_at(selection_x, selection_z);
        if (column_top <= column_base)
        {
            return std::nullopt;
        }

        return VoxelSelection{selection_x, column_top - 1, selection_z};
    };

    std::optional<grannys_house_trials::sim::TargetId> action_target = command->target;
    if (!action_target)
    {
        constexpr std::string_view move_prefix = "move_";
        const std::string_view action_id_view(command->action_id);
        if (action_id_view.rfind(move_prefix, 0) == 0)
        {
            action_target = grannys_house_trials::sim::from_string(action_id_view.substr(move_prefix.size()));
        }
    }

    // Keep clipboard-driven targeted commands aligned with right-click selection state.
    if (!command->selection && action_target && command->action_id != "select_block")
    {
        if (const auto maybe_selection = selection_for_target(*action_target))
        {
            selected_voxel_ = *maybe_selection;
        }
    }

    if (command->action_id == "select_block")
    {
        if (!command->selection)
        {
            MessageBoxW(
                hwnd_,
                L"select_block requires selection_x, selection_y, and selection_z.",
                L"Agent JSON",
                MB_OK | MB_ICONERROR);
            return;
        }

        refresh_scenario_action_ui();
        refresh_info_panel();
        return;
    }

    static_cast<void>(yard_session_.run_action(command->action_id, command->target));
    rebuild_display_field_buffer();
    refresh_scenario_action_ui();
    refresh_info_panel();
}

std::optional<VoxelSelection> D3D12App::try_pick_voxel(POINT client_point) const
{
    const CameraMatrices matrices = build_camera_matrices(camera_, width_, height_);
    const XMMATRIX world = XMMatrixIdentity();
    const XMVECTOR near_screen = XMVectorSet(
        static_cast<float>(client_point.x),
        static_cast<float>(client_point.y),
        0.0f,
        1.0f);
    const XMVECTOR far_screen = XMVectorSet(
        static_cast<float>(client_point.x),
        static_cast<float>(client_point.y),
        1.0f,
        1.0f);
    const XMVECTOR far_world = XMVector3Unproject(
        far_screen,
        0.0f,
        0.0f,
        static_cast<float>(width_),
        static_cast<float>(height_),
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

    const float voxel_size = render_voxel_size_feet();
    const int grid_width = render_grid_width();
    const int grid_depth = render_grid_depth();

    float field_distance = 0.0f;
    const XMFLOAT3 field_min{field_origin_x_, 0.0f, field_origin_z_};
    const XMFLOAT3 field_max{
        field_origin_x_ + static_cast<float>(grid_width) * voxel_size,
        max_column_height_voxels_ * voxel_size,
        field_origin_z_ + static_cast<float>(grid_depth) * voxel_size};

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
        static_cast<int>(std::floor((current_position.x - field_origin_x_) / voxel_size)),
        0,
        grid_width - 1);
    int cell_z = std::clamp(
        static_cast<int>(std::floor((current_position.z - field_origin_z_) / voxel_size)),
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
            field_origin_x_ + static_cast<float>(step_x > 0 ? (cell_x + 1) : cell_x) * voxel_size;
        t_max_x = (next_boundary_x - ray.origin.x) / ray.direction.x;
        t_delta_x = voxel_size / std::fabs(ray.direction.x);
    }

    if (step_z != 0)
    {
        const float next_boundary_z =
            field_origin_z_ + static_cast<float>(step_z > 0 ? (cell_z + 1) : cell_z) * voxel_size;
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

        if (display_grid_mode_ == DisplayGridMode::erosion_inch_columns)
        {
            const int patch_index = refined_patch_field_.patch_index_at(cell_x, cell_z);
            if (patch_index != grannys_house_trials::sim::SparseRefinedPatchField::invalid_patch_index)
            {
                const auto &patch = refined_patch_field_.patch_at_index(patch_index);
                const int patch_resolution = refined_patch_field_.patch_resolution();
                const float fine_voxel_size = field_.voxel_size_feet() / static_cast<float>(patch_resolution);
                const float patch_base = static_cast<float>(patch.coarse_full_height_inches) * fine_voxel_size;
                const float patch_top = static_cast<float>(patch.max_height_inches) * fine_voxel_size;
                const float y_at_entry = ray.origin.y + current_t * ray.direction.y;
                const float y_at_exit = ray.origin.y + cell_exit_t * ray.direction.y;
                const float segment_min_y = std::min(y_at_entry, y_at_exit);
                const float segment_max_y = std::max(y_at_entry, y_at_exit);

                if (patch_top > patch_base + 0.0001f && segment_max_y >= patch_base && segment_min_y <= patch_top)
                {
                    const float cell_min_x = field_origin_x_ + static_cast<float>(cell_x) * voxel_size;
                    const float cell_min_z = field_origin_z_ + static_cast<float>(cell_z) * voxel_size;
                    const XMFLOAT3 local_position{
                        ray.origin.x + ray.direction.x * (current_t + 0.0005f),
                        ray.origin.y + ray.direction.y * (current_t + 0.0005f),
                        ray.origin.z + ray.direction.z * (current_t + 0.0005f)};
                    int local_x = std::clamp(
                        static_cast<int>(std::floor((local_position.x - cell_min_x) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);
                    int local_z = std::clamp(
                        static_cast<int>(std::floor((local_position.z - cell_min_z) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);

                    const int local_step_x = ray.direction.x > 0.0f ? 1 : (ray.direction.x < 0.0f ? -1 : 0);
                    const int local_step_z = ray.direction.z > 0.0f ? 1 : (ray.direction.z < 0.0f ? -1 : 0);
                    float local_t_max_x = large_t;
                    float local_t_max_z = large_t;
                    float local_t_delta_x = large_t;
                    float local_t_delta_z = large_t;

                    if (local_step_x != 0)
                    {
                        const float next_boundary_x =
                            cell_min_x + static_cast<float>(local_step_x > 0 ? (local_x + 1) : local_x) * fine_voxel_size;
                        local_t_max_x = (next_boundary_x - ray.origin.x) / ray.direction.x;
                        local_t_delta_x = fine_voxel_size / std::fabs(ray.direction.x);
                    }

                    if (local_step_z != 0)
                    {
                        const float next_boundary_z =
                            cell_min_z + static_cast<float>(local_step_z > 0 ? (local_z + 1) : local_z) * fine_voxel_size;
                        local_t_max_z = (next_boundary_z - ray.origin.z) / ray.direction.z;
                        local_t_delta_z = fine_voxel_size / std::fabs(ray.direction.z);
                    }

                    float local_current_t = current_t;
                    const int local_max_steps = patch_resolution * 2 + 8;
                    for (int local_iteration = 0; local_iteration < local_max_steps; ++local_iteration)
                    {
                        if (local_x < 0 || local_x >= patch_resolution || local_z < 0 || local_z >= patch_resolution)
                        {
                            break;
                        }

                        const float local_cell_exit_t = std::min(std::min(local_t_max_x, local_t_max_z), cell_exit_t);
                        const int patch_height_index = local_z * patch_resolution + local_x;
                        const float column_top =
                            static_cast<float>(patch.top_heights_inches[static_cast<std::size_t>(patch_height_index)])
                            * fine_voxel_size;

                        if (column_top > patch_base + 0.0001f)
                        {
                            const float local_y_at_entry = ray.origin.y + local_current_t * ray.direction.y;
                            const float local_y_at_exit = ray.origin.y + local_cell_exit_t * ray.direction.y;
                            const float local_segment_min_y = std::min(local_y_at_entry, local_y_at_exit);
                            const float local_segment_max_y = std::max(local_y_at_entry, local_y_at_exit);

                            if (local_segment_max_y >= patch_base && local_segment_min_y <= column_top)
                            {
                                float hit_t = local_current_t;

                                if (ray.direction.y > 0.0f && local_y_at_entry < patch_base)
                                {
                                    hit_t = std::max(hit_t, (patch_base - ray.origin.y) / ray.direction.y);
                                }

                                if (ray.direction.y < 0.0f && local_y_at_entry > column_top)
                                {
                                    hit_t = std::max(hit_t, (column_top - ray.origin.y) / ray.direction.y);
                                }

                                if (hit_t <= local_cell_exit_t + 0.0001f)
                                {
                                    const float hit_y = ray.origin.y + ray.direction.y * hit_t;
                                    const int voxel_y = std::clamp(
                                        static_cast<int>(std::floor(std::max(hit_y - 0.001f, patch_base) / fine_voxel_size)),
                                        static_cast<int>(patch.coarse_full_height_inches),
                                        static_cast<int>(patch.top_heights_inches[static_cast<std::size_t>(patch_height_index)]) - 1);
                                    return VoxelSelection{
                                        cell_x * patch_resolution + local_x,
                                        voxel_y,
                                        cell_z * patch_resolution + local_z};
                                }
                            }
                        }

                        if (local_t_max_x < local_t_max_z)
                        {
                            local_current_t = local_t_max_x;
                            local_t_max_x += local_t_delta_x;
                            local_x += local_step_x;
                        }
                        else
                        {
                            local_current_t = local_t_max_z;
                            local_t_max_z += local_t_delta_z;
                            local_z += local_step_z;
                        }

                        if (local_current_t > cell_exit_t)
                        {
                            break;
                        }
                    }
                }
            }
        }
        else if (display_grid_mode_ == DisplayGridMode::hybrid_adaptive_columns)
        {
            const int patch_resolution = refined_patch_field_.patch_resolution();
            const float fine_voxel_size = field_.voxel_size_feet() / static_cast<float>(patch_resolution);
            const float cell_min_x = field_origin_x_ + static_cast<float>(cell_x) * voxel_size;
            const float cell_min_z = field_origin_z_ + static_cast<float>(cell_z) * voxel_size;
            const int patch_index = refined_patch_field_.patch_index_at(cell_x, cell_z);

            std::optional<VoxelSelection> refined_selection;
            if (patch_index != grannys_house_trials::sim::SparseRefinedPatchField::invalid_patch_index)
            {
                const auto &patch = refined_patch_field_.patch_at_index(patch_index);
                const float patch_base = static_cast<float>(patch.coarse_full_height_inches) * fine_voxel_size;
                const float patch_top = static_cast<float>(patch.max_height_inches) * fine_voxel_size;
                const float y_at_entry = ray.origin.y + current_t * ray.direction.y;
                const float y_at_exit = ray.origin.y + cell_exit_t * ray.direction.y;
                const float segment_min_y = std::min(y_at_entry, y_at_exit);
                const float segment_max_y = std::max(y_at_entry, y_at_exit);

                if (patch_top > patch_base + 0.0001f && segment_max_y >= patch_base && segment_min_y <= patch_top)
                {
                    const XMFLOAT3 local_position{
                        ray.origin.x + ray.direction.x * (current_t + 0.0005f),
                        ray.origin.y + ray.direction.y * (current_t + 0.0005f),
                        ray.origin.z + ray.direction.z * (current_t + 0.0005f)};
                    int local_x = std::clamp(
                        static_cast<int>(std::floor((local_position.x - cell_min_x) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);
                    int local_z = std::clamp(
                        static_cast<int>(std::floor((local_position.z - cell_min_z) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);

                    const int local_step_x = ray.direction.x > 0.0f ? 1 : (ray.direction.x < 0.0f ? -1 : 0);
                    const int local_step_z = ray.direction.z > 0.0f ? 1 : (ray.direction.z < 0.0f ? -1 : 0);
                    float local_t_max_x = large_t;
                    float local_t_max_z = large_t;
                    float local_t_delta_x = large_t;
                    float local_t_delta_z = large_t;

                    if (local_step_x != 0)
                    {
                        const float next_boundary_x =
                            cell_min_x + static_cast<float>(local_step_x > 0 ? (local_x + 1) : local_x) * fine_voxel_size;
                        local_t_max_x = (next_boundary_x - ray.origin.x) / ray.direction.x;
                        local_t_delta_x = fine_voxel_size / std::fabs(ray.direction.x);
                    }

                    if (local_step_z != 0)
                    {
                        const float next_boundary_z =
                            cell_min_z + static_cast<float>(local_step_z > 0 ? (local_z + 1) : local_z) * fine_voxel_size;
                        local_t_max_z = (next_boundary_z - ray.origin.z) / ray.direction.z;
                        local_t_delta_z = fine_voxel_size / std::fabs(ray.direction.z);
                    }

                    float local_current_t = current_t;
                    const int local_max_steps = patch_resolution * 2 + 8;
                    for (int local_iteration = 0; local_iteration < local_max_steps; ++local_iteration)
                    {
                        if (local_x < 0 || local_x >= patch_resolution || local_z < 0 || local_z >= patch_resolution)
                        {
                            break;
                        }

                        const float local_cell_exit_t = std::min(std::min(local_t_max_x, local_t_max_z), cell_exit_t);
                        const int patch_height_index = local_z * patch_resolution + local_x;
                        const float column_top =
                            static_cast<float>(patch.top_heights_inches[static_cast<std::size_t>(patch_height_index)])
                            * fine_voxel_size;

                        if (column_top > patch_base + 0.0001f)
                        {
                            const float local_y_at_entry = ray.origin.y + local_current_t * ray.direction.y;
                            const float local_y_at_exit = ray.origin.y + local_cell_exit_t * ray.direction.y;
                            const float local_segment_min_y = std::min(local_y_at_entry, local_y_at_exit);
                            const float local_segment_max_y = std::max(local_y_at_entry, local_y_at_exit);

                            if (local_segment_max_y >= patch_base && local_segment_min_y <= column_top)
                            {
                                float hit_t = local_current_t;

                                if (ray.direction.y > 0.0f && local_y_at_entry < patch_base)
                                {
                                    hit_t = std::max(hit_t, (patch_base - ray.origin.y) / ray.direction.y);
                                }

                                if (ray.direction.y < 0.0f && local_y_at_entry > column_top)
                                {
                                    hit_t = std::max(hit_t, (column_top - ray.origin.y) / ray.direction.y);
                                }

                                if (hit_t <= local_cell_exit_t + 0.0001f)
                                {
                                    const float hit_y = ray.origin.y + ray.direction.y * hit_t;
                                    const int voxel_y = std::clamp(
                                        static_cast<int>(std::floor(std::max(hit_y - 0.001f, 0.0f) / fine_voxel_size)),
                                        0,
                                        static_cast<int>(patch.top_heights_inches[static_cast<std::size_t>(patch_height_index)]) - 1);
                                    refined_selection = VoxelSelection{
                                        cell_x * patch_resolution + local_x,
                                        voxel_y,
                                        cell_z * patch_resolution + local_z};
                                    break;
                                }
                            }
                        }

                        if (local_t_max_x < local_t_max_z)
                        {
                            local_current_t = local_t_max_x;
                            local_t_max_x += local_t_delta_x;
                            local_x += local_step_x;
                        }
                        else
                        {
                            local_current_t = local_t_max_z;
                            local_t_max_z += local_t_delta_z;
                            local_z += local_step_z;
                        }

                        if (local_current_t > cell_exit_t)
                        {
                            break;
                        }
                    }
                }
            }

            const int coarse_full_block_count = ownership_field_.full_block_count_at(cell_x, cell_z);
            const int coarse_full_height_inches = coarse_full_block_count * patch_resolution;
            const float coarse_column_top = static_cast<float>(coarse_full_block_count) * voxel_size;
            const float y_at_entry = ray.origin.y + current_t * ray.direction.y;
            const float y_at_exit = ray.origin.y + cell_exit_t * ray.direction.y;
            const float segment_min_y = std::min(y_at_entry, y_at_exit);
            const float segment_max_y = std::max(y_at_entry, y_at_exit);

            std::optional<VoxelSelection> coarse_selection;
            if (coarse_column_top > 0.0001f && segment_max_y >= 0.0f && segment_min_y <= coarse_column_top)
            {
                float hit_t = current_t;

                if (ray.direction.y > 0.0f && y_at_entry < 0.0f)
                {
                    hit_t = std::max(hit_t, (0.0f - ray.origin.y) / ray.direction.y);
                }

                if (ray.direction.y < 0.0f && y_at_entry > coarse_column_top)
                {
                    hit_t = std::max(hit_t, (coarse_column_top - ray.origin.y) / ray.direction.y);
                }

                if (hit_t <= cell_exit_t + 0.0001f)
                {
                    const float hit_y = ray.origin.y + ray.direction.y * hit_t;
                    const int local_x = std::clamp(
                        static_cast<int>(std::floor((ray.origin.x + ray.direction.x * hit_t - cell_min_x) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);
                    const int local_z = std::clamp(
                        static_cast<int>(std::floor((ray.origin.z + ray.direction.z * hit_t - cell_min_z) / fine_voxel_size)),
                        0,
                        patch_resolution - 1);
                    const int voxel_y = std::clamp(
                        static_cast<int>(std::floor(std::max(hit_y - 0.001f, 0.0f) / fine_voxel_size)),
                        0,
                        std::max(coarse_full_height_inches - 1, 0));
                    coarse_selection = VoxelSelection{
                        cell_x * patch_resolution + local_x,
                        voxel_y,
                        cell_z * patch_resolution + local_z};
                }
            }

            if (refined_selection && coarse_selection)
            {
                const float refined_hit_y = static_cast<float>(refined_selection->y) * fine_voxel_size;
                const float coarse_hit_y = static_cast<float>(coarse_selection->y) * fine_voxel_size;
                return refined_hit_y >= coarse_hit_y ? *refined_selection : *coarse_selection;
            }

            if (refined_selection)
            {
                return refined_selection;
            }

            if (coarse_selection)
            {
                return coarse_selection;
            }
        }
        else
        {
            const int column_base_voxels = display_column_base_voxels_at(cell_x, cell_z);
            const int column_height_voxels = display_column_height_voxels_at(cell_x, cell_z);

            if (column_height_voxels > column_base_voxels)
            {
                const float column_base = static_cast<float>(column_base_voxels) * voxel_size;
                const float column_top = static_cast<float>(column_height_voxels) * voxel_size;
                const float y_at_entry = ray.origin.y + current_t * ray.direction.y;
                const float y_at_exit = ray.origin.y + cell_exit_t * ray.direction.y;
                const float segment_min_y = std::min(y_at_entry, y_at_exit);
                const float segment_max_y = std::max(y_at_entry, y_at_exit);

                if (segment_max_y >= column_base && segment_min_y <= column_top)
                {
                    float hit_t = current_t;

                    if (ray.direction.y > 0.0f && y_at_entry < column_base)
                    {
                        hit_t = std::max(hit_t, (column_base - ray.origin.y) / ray.direction.y);
                    }

                    if (ray.direction.y < 0.0f && y_at_entry > column_top)
                    {
                        hit_t = std::max(hit_t, (column_top - ray.origin.y) / ray.direction.y);
                    }

                    if (hit_t <= cell_exit_t + 0.0001f)
                    {
                        const float hit_y = ray.origin.y + ray.direction.y * hit_t;
                        const int voxel_y = std::clamp(
                            static_cast<int>(std::floor(std::max(hit_y - 0.001f, column_base) / voxel_size)),
                            column_base_voxels,
                            column_height_voxels - 1);
                        return VoxelSelection{cell_x, voxel_y, cell_z};
                    }
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

void D3D12App::render()
{
    if (width_ == 0 || height_ == 0 || IsIconic(hwnd_) || !context_)
    {
        return;
    }

    const float clear_color[] = {0.66f, 0.80f, 0.97f, 1.0f};
    ui_frame_renderer_.render(
        *context_,
        cbv_heap_.Get(),
        clear_color,
        [&](ID3D12GraphicsCommandList *command_list) {
            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
            command_list->RSSetViewports(1, &viewport);
            command_list->RSSetScissorRects(1, &scissor);
            command_list->SetPipelineState(active_pipeline_state());
            command_list->SetGraphicsRootSignature(root_signature_.Get());
            command_list->SetGraphicsRootConstantBufferView(0, constant_buffer_->GetGPUVirtualAddress());
            command_list->SetGraphicsRootDescriptorTable(1, cbv_heap_->GetGPUDescriptorHandleForHeapStart());
            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list->DrawInstanced(3, 1, 0, 0);
        });
}

void D3D12App::on_mouse_down(MouseState::DragMode drag_mode, LPARAM lparam)
{
    mouse_.left_button_down = drag_mode == MouseState::DragMode::pan;
    mouse_.right_button_down = drag_mode == MouseState::DragMode::orbit;
    mouse_.dragging = false;
    mouse_.drag_mode = drag_mode;
    mouse_.anchor_position.x = GET_X_LPARAM(lparam);
    mouse_.anchor_position.y = GET_Y_LPARAM(lparam);
    mouse_.last_position.x = GET_X_LPARAM(lparam);
    mouse_.last_position.y = GET_Y_LPARAM(lparam);
    SetFocus(viewport_hwnd_);
    SetCapture(viewport_hwnd_);
}

void D3D12App::on_mouse_up(MouseState::DragMode drag_mode, LPARAM lparam)
{
    if (drag_mode == MouseState::DragMode::pan && mouse_.left_button_down && !mouse_.dragging)
    {
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        selected_voxel_ = try_pick_voxel(point);
        refresh_scenario_action_ui();
        refresh_info_panel();
    }

    mouse_.left_button_down = false;
    mouse_.right_button_down = false;
    mouse_.dragging = false;
    mouse_.drag_mode = MouseState::DragMode::none;
    ReleaseCapture();
}

void D3D12App::on_mouse_move(WPARAM buttons, LPARAM lparam)
{
    const bool is_panning = mouse_.drag_mode == MouseState::DragMode::pan
        && mouse_.left_button_down
        && (buttons & MK_LBUTTON) != 0;
    const bool is_orbiting = mouse_.drag_mode == MouseState::DragMode::orbit
        && mouse_.right_button_down
        && (buttons & MK_RBUTTON) != 0;

    if (!is_panning && !is_orbiting)
    {
        return;
    }

    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

    if (!mouse_.dragging)
    {
        const int drag_distance_x = std::abs(point.x - mouse_.anchor_position.x);
        const int drag_distance_y = std::abs(point.y - mouse_.anchor_position.y);
        mouse_.dragging = drag_distance_x >= 4 || drag_distance_y >= 4;
    }

    if (!mouse_.dragging)
    {
        return;
    }

    const int dx = point.x - mouse_.last_position.x;
    const int dy = point.y - mouse_.last_position.y;
    mouse_.last_position = point;

    if (is_panning)
    {
        const CameraMatrices matrices = build_camera_matrices(camera_, width_, height_);
        XMVECTOR forward = XMVectorSubtract(
            XMLoadFloat3(&matrices.target_position),
            XMLoadFloat3(&matrices.eye_position));
        forward = XMVectorSetY(forward, 0.0f);
        forward = XMVector3Normalize(forward);
        const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
        const float pan_scale = camera_.distance()
            / static_cast<float>(std::max<UINT>(1, std::min(width_, height_)))
            * 1.9f;
        const XMVECTOR pan_delta = XMVectorAdd(
            XMVectorScale(right, static_cast<float>(-dx) * pan_scale),
            XMVectorScale(forward, static_cast<float>(dy) * pan_scale));

        XMFLOAT3 delta{};
        XMStoreFloat3(&delta, pan_delta);
        camera_.move_focus(delta.x, 0.0f, delta.z);
        return;
    }

    camera_.orbit(
        static_cast<float>(-dx) * 0.35f,
        static_cast<float>(dy) * 0.25f);
}

void D3D12App::on_mouse_wheel(WPARAM wparam)
{
    const short wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
    camera_.zoom(static_cast<float>(-wheel_delta) * 0.01f);
}

void D3D12App::reset_camera()
{
    camera_ = make_default_camera(field_.width(), field_.depth());
}

void D3D12App::step_erosion_cycle()
{
    erosion_field_.step_cycle();
    ownership_field_.rebuild(field_, erosion_field_);
    refined_patch_field_.rebuild(field_, erosion_field_, ownership_field_);
    if (display_grid_mode_ != DisplayGridMode::coarse_foot_columns)
    {
        rebuild_display_field_buffer();
    }
    refresh_info_panel();
}

LRESULT D3D12App::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        if (hwnd == viewport_hwnd_)
        {
            on_mouse_down(MouseState::DragMode::pan, lparam);
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        if (hwnd == viewport_hwnd_)
        {
            on_mouse_down(MouseState::DragMode::orbit, lparam);
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case control_id_reset_camera:
            reset_camera();
            return 0;
        case control_id_step_erosion:
            step_erosion_cycle();
            return 0;
        case control_id_clear_selection:
            selected_voxel_.reset();
            refresh_scenario_action_ui();
            refresh_info_panel();
            return 0;
        case control_id_display_grid_combo:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                const int selection = ComboBox_GetCurSel(display_grid_combo_);
                switch (selection)
                {
                case 0:
                    set_display_grid_mode(DisplayGridMode::coarse_foot_columns);
                    break;
                case 1:
                    set_display_grid_mode(DisplayGridMode::erosion_inch_columns);
                    break;
                case 2:
                    set_display_grid_mode(DisplayGridMode::hybrid_adaptive_columns);
                    break;
                default:
                    break;
                }
            }
            return 0;
        case control_id_field_size_combo:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                const int selection = ComboBox_GetCurSel(field_size_combo_);
                switch (selection)
                {
                case 0:
                    apply_field_size(50);
                    break;
                case 1:
                    apply_field_size(100);
                    break;
                case 2:
                    apply_field_size(150);
                    break;
                case 3:
                    apply_field_size(200);
                    break;
                case 4:
                    apply_field_size(1000);
                    break;
                default:
                    break;
                }
            }
            return 0;
        case control_id_toggle_highlight:
            highlight_selection_ = Button_GetCheck(highlight_checkbox_) == BST_CHECKED;
            refresh_info_panel();
            return 0;
        case control_id_toggle_fog:
            fog_enabled_ = Button_GetCheck(fog_checkbox_) == BST_CHECKED;
            refresh_info_panel();
            return 0;
        case control_id_toggle_packet_view:
            packet_view_enabled_ = Button_GetCheck(packet_view_checkbox_) == BST_CHECKED;
            refresh_info_panel();
            return 0;
        case control_id_copy_agent_snapshot:
            if (!latest_agent_snapshot_.empty())
            {
                const bool copied = copy_text_to_clipboard(hwnd_, latest_agent_snapshot_);
                if (!copied)
                {
                    MessageBeep(MB_ICONWARNING);
                }
            }
            return 0;
        case control_id_run_agent_json:
            apply_agent_json_from_clipboard();
            return 0;
        case control_id_scenario_action_combo:
            if (HIWORD(wparam) == CBN_SELCHANGE)
            {
                refresh_info_panel();
            }
            return 0;
        case control_id_run_action:
            run_selected_scenario_action();
            return 0;
        case control_id_advance_round:
            run_scenario_action("advance_simulation");
            return 0;
        case control_id_end_round:
            run_scenario_action("end_round");
            return 0;
        case control_id_reset_round:
            run_scenario_action("reset_round");
            return 0;
        default:
            break;
        }
        break;
    case WM_ERASEBKGND:
        if (hwnd == viewport_hwnd_)
        {
            return 1;
        }
        break;
    case WM_LBUTTONDBLCLK:
        return 0;
    case WM_LBUTTONUP:
        if (hwnd == viewport_hwnd_)
        {
            on_mouse_up(MouseState::DragMode::pan, lparam);
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        if (hwnd == viewport_hwnd_)
        {
            on_mouse_up(MouseState::DragMode::orbit, lparam);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (hwnd == viewport_hwnd_)
        {
            on_mouse_move(wparam, lparam);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        on_mouse_wheel(wparam);
        return 0;
    case WM_SIZE:
        if (hwnd == viewport_hwnd_)
        {
            if (wparam != SIZE_MINIMIZED)
            {
                on_viewport_resized(
                    static_cast<UINT>(LOWORD(lparam)),
                    static_cast<UINT>(HIWORD(lparam)));
            }
            return 0;
        }

        if (hwnd == hwnd_)
        {
            layout_ui();
            return 0;
        }
    case WM_MOVE:
        if (hwnd == hwnd_)
        {
            layout_ui();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
        {
            DestroyWindow(hwnd_);
        }
        else if (wparam == 'E')
        {
            step_erosion_cycle();
        }
        return 0;
    case WM_DESTROY:
        if (hwnd == hwnd_)
        {
            PostQuitMessage(0);
            return 0;
        }
        break;
    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK D3D12App::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCCREATE)
    {
        const auto *create_struct = reinterpret_cast<CREATESTRUCT *>(lparam);
        auto *app = static_cast<D3D12App *>(create_struct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    auto *app = reinterpret_cast<D3D12App *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    return app ? app->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    try
    {
        D3D12App app;
        return app.run(instance);
    }
    catch (const std::exception &exception)
    {
        MessageBoxA(nullptr, exception.what(), "Grass Field 001 failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}

