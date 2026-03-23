#include "dashboard.h"

#define NOMINMAX
#include <Windows.h>
#include <conio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace voxel_infrastructure_004
{
namespace
{
struct RgbColor
{
    int red = 0;
    int green = 0;
    int blue = 0;
};

class AlternateScreenSession
{
  public:
    AlternateScreenSession()
    {
        stdout_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
        stdin_handle_ = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(stdout_handle_, &original_out_mode_);
        GetConsoleMode(stdin_handle_, &original_in_mode_);

        DWORD out_mode = original_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(stdout_handle_, out_mode);

        DWORD in_mode = original_in_mode_;
        in_mode &= ~ENABLE_ECHO_INPUT;
        in_mode &= ~ENABLE_LINE_INPUT;
        SetConsoleMode(stdin_handle_, in_mode);

        std::cout << "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
        std::cout.flush();
    }

    ~AlternateScreenSession()
    {
        std::cout << "\x1b[?25h\x1b[?1049l";
        std::cout.flush();
        SetConsoleMode(stdout_handle_, original_out_mode_);
        SetConsoleMode(stdin_handle_, original_in_mode_);
    }

  private:
    HANDLE stdout_handle_ = INVALID_HANDLE_VALUE;
    HANDLE stdin_handle_ = INVALID_HANDLE_VALUE;
    DWORD original_out_mode_ = 0;
    DWORD original_in_mode_ = 0;
};

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

std::string reset_ansi()
{
    return "\x1b[0m";
}

std::string ansi_fg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[38;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

std::string ansi_bg(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[48;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

std::string color_text(const std::string &text, RgbColor color)
{
    return ansi_fg(color) + text + reset_ansi();
}

std::string badge(const std::string &text, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + ' ' + text + ' ' + reset_ansi();
}

std::string pad_right(const std::string &text, int width)
{
    if (static_cast<int>(text.size()) >= width)
    {
        return text.substr(0, static_cast<std::size_t>(width));
    }

    return text + std::string(static_cast<std::size_t>(width - static_cast<int>(text.size())), ' ');
}

int visible_length(const std::string &text)
{
    int length = 0;
    bool in_escape = false;
    for (char ch : text)
    {
        if (!in_escape)
        {
            if (ch == '\x1b')
            {
                in_escape = true;
            }
            else
            {
                ++length;
            }
        }
        else if (ch == 'm')
        {
            in_escape = false;
        }
    }
    return length;
}

std::string pad_ansi(const std::string &text, int width)
{
    const int length = visible_length(text);
    if (length >= width)
    {
        return text;
    }

    return text + std::string(static_cast<std::size_t>(width - length), ' ');
}

std::string section_bar(const std::string &text, int width, RgbColor fg, RgbColor bg)
{
    return ansi_bg(bg) + ansi_fg(fg) + pad_right(" " + text + " ", width) + reset_ansi();
}

std::string format_fixed(double value, int precision = 2)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

RgbColor blend(RgbColor a, RgbColor b, double t)
{
    const double clamped = clamp01(t);
    const auto channel = [clamped](int start, int end) {
        return static_cast<int>(std::lround(static_cast<double>(start) + static_cast<double>(end - start) * clamped));
    };
    return {channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue)};
}

std::string overlay_name(OverlayMode overlay)
{
    switch (overlay)
    {
    case OverlayMode::Material: return "Material";
    case OverlayMode::Saturation: return "Saturation";
    case OverlayMode::Pressure: return "Pressure";
    case OverlayMode::StructuralRisk: return "Structural Risk";
    }
    return "Unknown";
}

std::string view_name(ViewMode view)
{
    switch (view)
    {
    case ViewMode::XYSliceAtZ: return "XY Slice (z plane)";
    case ViewMode::YZSliceAtX: return "YZ Slice (x plane)";
    case ViewMode::XZSliceAtY: return "XZ Slice (y plane)";
    case ViewMode::Isometric: return "Isometric";
    }
    return "Unknown";
}

std::string material_name(MaterialKind material)
{
    switch (material)
    {
    case MaterialKind::Air: return "Air";
    case MaterialKind::SurfaceWater: return "Surface Water";
    case MaterialKind::MarshSoil: return "Marsh Soil";
    case MaterialKind::TerraceFill: return "Terrace Fill";
    case MaterialKind::Bedrock: return "Bedrock";
    case MaterialKind::AncientConduit: return "Ancient Conduit";
    case MaterialKind::DelayBasin: return "Delay Basin";
    case MaterialKind::Collector: return "Collector";
    case MaterialKind::InspectionShaft: return "Inspection Shaft";
    }
    return "Unknown";
}

struct AxisLegend
{
    std::string top;
    std::string bottom;
    std::string left;
    std::string right;
};

AxisLegend axis_legend(ViewMode view)
{
    switch (view)
    {
    case ViewMode::XYSliceAtZ:
        return {
            "+Y",
            "-Y    z plane",
            "-X",
            "+X",
        };
    case ViewMode::YZSliceAtX:
        return {
            "+Y",
            "-Y    x plane",
            "-Z",
            "+Z",
        };
    case ViewMode::XZSliceAtY:
        return {
            "+Z",
            "-Z    y plane",
            "-X",
            "+X",
        };
    case ViewMode::Isometric:
        return {
            "+Y",
            "-Y    iso",
            "+Z",
            "+X",
        };
    }

    return {};
}

char voxel_glyph(const VoxelState &voxel, OverlayMode overlay)
{
    if (voxel.hidden && !voxel.exposed)
    {
        return '?';
    }

    switch (overlay)
    {
    case OverlayMode::Material:
        switch (voxel.material)
        {
        case MaterialKind::Air: return ' ';
        case MaterialKind::SurfaceWater: return '~';
        case MaterialKind::MarshSoil: return 'm';
        case MaterialKind::TerraceFill: return 't';
        case MaterialKind::Bedrock: return '#';
        case MaterialKind::AncientConduit: return '=';
        case MaterialKind::DelayBasin: return 'D';
        case MaterialKind::Collector: return 'C';
        case MaterialKind::InspectionShaft: return 'I';
        }
        break;
    case OverlayMode::Saturation:
        if (voxel.saturation < 0.1) return '.';
        if (voxel.saturation < 0.3) return ':';
        if (voxel.saturation < 0.55) return 'o';
        if (voxel.saturation < 0.8) return 'O';
        return '@';
    case OverlayMode::Pressure:
        if (voxel.pressure < 0.12) return '.';
        if (voxel.pressure < 0.28) return ':';
        if (voxel.pressure < 0.5) return 'o';
        if (voxel.pressure < 0.75) return 'O';
        return '@';
    case OverlayMode::StructuralRisk:
        if (voxel.stress < 0.12) return '.';
        if (voxel.stress < 0.3) return ':';
        if (voxel.stress < 0.55) return 'o';
        if (voxel.stress < 0.8) return 'O';
        return '@';
    }

    return '?';
}

RgbColor voxel_foreground(const VoxelState &voxel, OverlayMode overlay, bool selected)
{
    if (selected)
    {
        return {248, 234, 108};
    }
    if (voxel.hidden && !voxel.exposed)
    {
        return {196, 186, 146};
    }

    switch (overlay)
    {
    case OverlayMode::Material:
        switch (voxel.material)
        {
        case MaterialKind::Air: return {46, 52, 68};
        case MaterialKind::SurfaceWater: return {102, 168, 238};
        case MaterialKind::MarshSoil: return {116, 186, 120};
        case MaterialKind::TerraceFill: return {214, 184, 124};
        case MaterialKind::Bedrock: return {168, 166, 176};
        case MaterialKind::AncientConduit: return {116, 222, 214};
        case MaterialKind::DelayBasin: return {230, 204, 120};
        case MaterialKind::Collector: return {204, 164, 244};
        case MaterialKind::InspectionShaft: return {244, 240, 228};
        }
        break;
    case OverlayMode::Saturation:
        return blend({128, 126, 112}, {74, 156, 238}, voxel.saturation);
    case OverlayMode::Pressure:
        return blend({150, 150, 158}, {244, 174, 84}, voxel.pressure);
    case OverlayMode::StructuralRisk:
        return blend({118, 198, 132}, {228, 96, 88}, voxel.stress);
    }

    return {255, 255, 255};
}

RgbColor voxel_background(const VoxelState &voxel, OverlayMode overlay, bool selected)
{
    if (selected)
    {
        return {112, 58, 20};
    }
    if (voxel.hidden && !voxel.exposed)
    {
        return {38, 32, 24};
    }

    switch (overlay)
    {
    case OverlayMode::Material:
        switch (voxel.material)
        {
        case MaterialKind::Air: return {12, 18, 28};
        case MaterialKind::SurfaceWater: return {24, 64, 128};
        case MaterialKind::MarshSoil: return {24, 76, 40};
        case MaterialKind::TerraceFill: return {92, 62, 24};
        case MaterialKind::Bedrock: return {56, 56, 64};
        case MaterialKind::AncientConduit: return {18, 86, 94};
        case MaterialKind::DelayBasin: return {94, 82, 26};
        case MaterialKind::Collector: return {72, 44, 104};
        case MaterialKind::InspectionShaft: return {88, 88, 94};
        }
        break;
    case OverlayMode::Saturation:
        return blend({28, 22, 18}, {42, 104, 182}, voxel.saturation);
    case OverlayMode::Pressure:
        return blend({22, 26, 54}, {178, 82, 24}, voxel.pressure);
    case OverlayMode::StructuralRisk:
        return blend({22, 54, 32}, {126, 26, 24}, voxel.stress);
    }

    return {18, 18, 22};
}

const VoxelState &voxel_at(const SimulationSnapshot &snapshot, int x, int y, int z)
{
    const int index = (z * snapshot.height + y) * snapshot.width + x;
    return snapshot.voxels[static_cast<std::size_t>(index)];
}

bool is_hidden_by_camera(ViewMode view, const UiState &ui, int x, int y, int z, const SimulationSnapshot &snapshot)
{
    switch (view)
    {
    case ViewMode::XYSliceAtZ:
        return z < ui.hidden_depth;
    case ViewMode::YZSliceAtX:
        return x < ui.hidden_depth;
    case ViewMode::XZSliceAtY:
        return y < ui.hidden_depth;
    case ViewMode::Isometric:
        return (x + y + z) < ui.hidden_depth * 3;
    }

    return false;
}

bool is_selected_voxel(const UiState &ui, int x, int y, int z)
{
    return x == ui.sample_x && y == ui.sample_y && z == ui.sample_z;
}

struct ProjectedCell
{
    int x = 0;
    int y = 0;
    int z = 0;
    bool valid = false;
};

ProjectedCell projected_cell_at(const UiState &ui, const SimulationSnapshot &snapshot, int u, int v)
{
    switch (ui.view)
    {
    case ViewMode::XYSliceAtZ:
        if (u >= 0 && u < snapshot.width && v >= 0 && v < snapshot.height)
        {
            return {u, v, ui.plane_z, true};
        }
        break;
    case ViewMode::YZSliceAtX:
        if (u >= 0 && u < snapshot.depth && v >= 0 && v < snapshot.height)
        {
            return {ui.plane_x, v, u, true};
        }
        break;
    case ViewMode::XZSliceAtY:
        if (u >= 0 && u < snapshot.width && v >= 0 && v < snapshot.depth)
        {
            return {u, ui.plane_y, v, true};
        }
        break;
    case ViewMode::Isometric:
        return {ui.sample_x, ui.sample_y, ui.sample_z, false};
    }

    return {};
}

void move_sample(UiState &ui, const SimulationSnapshot &snapshot, int du, int dv)
{
    switch (ui.view)
    {
    case ViewMode::XYSliceAtZ:
        ui.sample_x = std::clamp(ui.sample_x + du, 0, snapshot.width - 1);
        ui.sample_y = std::clamp(ui.sample_y + dv, 0, snapshot.height - 1);
        ui.sample_z = ui.plane_z;
        break;
    case ViewMode::YZSliceAtX:
        ui.sample_z = std::clamp(ui.sample_z + du, 0, snapshot.depth - 1);
        ui.sample_y = std::clamp(ui.sample_y + dv, 0, snapshot.height - 1);
        ui.sample_x = ui.plane_x;
        break;
    case ViewMode::XZSliceAtY:
        ui.sample_x = std::clamp(ui.sample_x + du, 0, snapshot.width - 1);
        ui.sample_z = std::clamp(ui.sample_z + dv, 0, snapshot.depth - 1);
        ui.sample_y = ui.plane_y;
        break;
    case ViewMode::Isometric:
        ui.sample_x = std::clamp(ui.sample_x + du, 0, snapshot.width - 1);
        ui.sample_y = std::clamp(ui.sample_y + dv, 0, snapshot.height - 1);
        break;
    }
}

void sync_sample_to_view(UiState &ui, const SimulationSnapshot &snapshot)
{
    switch (ui.view)
    {
    case ViewMode::XYSliceAtZ:
        ui.plane_z = std::clamp(ui.plane_z, 0, snapshot.depth - 1);
        ui.sample_z = ui.plane_z;
        break;
    case ViewMode::YZSliceAtX:
        ui.plane_x = std::clamp(ui.plane_x, 0, snapshot.width - 1);
        ui.sample_x = ui.plane_x;
        break;
    case ViewMode::XZSliceAtY:
        ui.plane_y = std::clamp(ui.plane_y, 0, snapshot.height - 1);
        ui.sample_y = ui.plane_y;
        break;
    case ViewMode::Isometric:
        break;
    }
}

int active_plane(const UiState &ui)
{
    switch (ui.view)
    {
    case ViewMode::XYSliceAtZ:
        return ui.plane_z;
    case ViewMode::YZSliceAtX:
        return ui.plane_x;
    case ViewMode::XZSliceAtY:
        return ui.plane_y;
    case ViewMode::Isometric:
        return ui.plane_y;
    }

    return 0;
}

void step_active_plane(UiState &ui, const SimulationSnapshot &snapshot, int delta)
{
    switch (ui.view)
    {
    case ViewMode::XYSliceAtZ:
        ui.plane_z = std::clamp(ui.plane_z + delta, 0, snapshot.depth - 1);
        break;
    case ViewMode::YZSliceAtX:
        ui.plane_x = std::clamp(ui.plane_x + delta, 0, snapshot.width - 1);
        break;
    case ViewMode::XZSliceAtY:
        ui.plane_y = std::clamp(ui.plane_y + delta, 0, snapshot.height - 1);
        break;
    case ViewMode::Isometric:
        ui.plane_y = std::clamp(ui.plane_y + delta, 0, snapshot.height - 1);
        break;
    }
}

void draw_slice(std::ostringstream &out, const SimulationSnapshot &snapshot, const UiState &ui)
{
    constexpr int canvas_width = 44;
    constexpr int canvas_height = 14;
    out << section_bar(" VOXEL VIEW ", canvas_width, {236, 242, 246}, {40, 60, 78}) << "\n";

    const AxisLegend legend = axis_legend(ui.view);
    const int top_pad = std::max(0, (canvas_width - static_cast<int>(legend.top.size())) / 2);
    out << " " << std::string(static_cast<std::size_t>(top_pad), ' ') << legend.top << "\n";

    auto emit_canvas_row = [&](int row_index,
                               const std::vector<std::string> &glyphs,
                               const std::vector<std::string> &fg_map,
                               const std::vector<std::string> &bg_map,
                               const std::vector<RgbColor> &palette) {
        const std::string left = row_index == canvas_height / 2 ? legend.left : "";
        const std::string right = row_index == canvas_height / 2 ? legend.right : "";
        out << pad_right(left, 4) << " ";
        for (int col = 0; col < canvas_width; ++col)
        {
            const char glyph = glyphs[static_cast<std::size_t>(row_index)][static_cast<std::size_t>(col)];
            if (glyph == ' ')
            {
                out << "  ";
                continue;
            }

            const RgbColor fg = palette[static_cast<std::size_t>(fg_map[static_cast<std::size_t>(row_index)][static_cast<std::size_t>(col)] - 1)];
            const RgbColor bg = palette[static_cast<std::size_t>(bg_map[static_cast<std::size_t>(row_index)][static_cast<std::size_t>(col)] - 1)];
            out << ansi_bg(bg) << ansi_fg(fg) << ' ' << glyph << reset_ansi();
        }
        out << " " << right << "\n";
    };

    if (ui.view == ViewMode::Isometric)
    {
        std::vector<std::string> glyphs(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), ' '));
        std::vector<std::string> fg_map(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), '\0'));
        std::vector<std::string> bg_map(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), '\0'));
        std::vector<RgbColor> palette;

        auto color_id = [&](RgbColor color) {
            for (std::size_t index = 0; index < palette.size(); ++index)
            {
                if (palette[index].red == color.red && palette[index].green == color.green && palette[index].blue == color.blue)
                {
                    return static_cast<char>(index + 1);
                }
            }
            palette.push_back(color);
            return static_cast<char>(palette.size());
        };

        for (int z = snapshot.depth - 1; z >= 0; --z)
        {
            for (int y = snapshot.height - 1; y >= 0; --y)
            {
                for (int x = 0; x < snapshot.width; ++x)
                {
                    if (is_hidden_by_camera(ui.view, ui, x, y, z, snapshot))
                    {
                        continue;
                    }

                    const int px = 4 + (x * 2) + (snapshot.depth - 1 - z);
                    const int py = 1 + (snapshot.height - 1 - y) + ((x + z) / 3);
                    if (py < 0 || py >= canvas_height || px < 0 || px >= canvas_width)
                    {
                        continue;
                    }

                    const VoxelState &voxel = voxel_at(snapshot, x, y, z);
                    const bool selected = is_selected_voxel(ui, x, y, z);
                    glyphs[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = voxel_glyph(voxel, ui.overlay);
                    fg_map[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = color_id(voxel_foreground(voxel, ui.overlay, selected));
                    bg_map[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = color_id(voxel_background(voxel, ui.overlay, selected));
                }
            }
        }

        for (int row = 0; row < canvas_height; ++row)
        {
            emit_canvas_row(row, glyphs, fg_map, bg_map, palette);
        }
        const int bottom_pad = std::max(0, (canvas_width - static_cast<int>(legend.bottom.size())) / 2);
        out << " " << std::string(static_cast<std::size_t>(bottom_pad), ' ') << legend.bottom << "\n";
        return;
    }

    const int u_max = (ui.view == ViewMode::YZSliceAtX) ? snapshot.depth : snapshot.width;
    const int v_max = (ui.view == ViewMode::XZSliceAtY) ? snapshot.depth : snapshot.height;
    const int x_offset = std::max(0, (canvas_width - u_max) / 2);
    const int y_offset = std::max(0, (canvas_height - v_max) / 2);
    std::vector<std::string> glyphs(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), ' '));
    std::vector<std::string> fg_map(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), '\0'));
    std::vector<std::string> bg_map(static_cast<std::size_t>(canvas_height), std::string(static_cast<std::size_t>(canvas_width), '\0'));
    std::vector<RgbColor> palette;

    auto color_id = [&](RgbColor color) {
        for (std::size_t index = 0; index < palette.size(); ++index)
        {
            if (palette[index].red == color.red && palette[index].green == color.green && palette[index].blue == color.blue)
            {
                return static_cast<char>(index + 1);
            }
        }
        palette.push_back(color);
        return static_cast<char>(palette.size());
    };

    for (int v = 0; v < v_max; ++v)
    {
        for (int u = 0; u < u_max; ++u)
        {
            const ProjectedCell cell = projected_cell_at(ui, snapshot, u, v);
            const VoxelState &voxel = voxel_at(snapshot, cell.x, cell.y, cell.z);
            const bool selected = is_selected_voxel(ui, cell.x, cell.y, cell.z);
            const bool hidden_by_camera = is_hidden_by_camera(ui.view, ui, cell.x, cell.y, cell.z, snapshot);
            const char glyph = hidden_by_camera ? ' ' : voxel_glyph(voxel, ui.overlay);
            const RgbColor fg = hidden_by_camera ? RgbColor{36, 36, 36} : voxel_foreground(voxel, ui.overlay, selected);
            const RgbColor bg = hidden_by_camera ? RgbColor{14, 14, 18} : voxel_background(voxel, ui.overlay, selected);
            const int px = x_offset + u;
            const int py = y_offset + (v_max - 1 - v);
            if (py >= 0 && py < canvas_height && px >= 0 && px < canvas_width)
            {
                glyphs[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = glyph;
                fg_map[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = color_id(fg);
                bg_map[static_cast<std::size_t>(py)][static_cast<std::size_t>(px)] = color_id(bg);
            }
        }
    }

    for (int row = 0; row < canvas_height; ++row)
    {
        emit_canvas_row(row, glyphs, fg_map, bg_map, palette);
    }

    const int bottom_pad = std::max(0, (canvas_width - static_cast<int>(legend.bottom.size())) / 2);
    out << " " << std::string(static_cast<std::size_t>(bottom_pad), ' ') << legend.bottom << "\n";
}

void append_boxed_lines(std::ostringstream &out, const std::string &title, const std::vector<std::string> &lines, int width)
{
    out << title << "\n";
    for (const std::string &line : lines)
    {
        out << pad_ansi(line, width) << "\n";
    }
}

void draw_frame(const SimulationSnapshot &snapshot, const UiState &ui)
{
    const int panel_width = 92;
    const int index = (ui.sample_z * snapshot.height + ui.sample_y) * snapshot.width + ui.sample_x;
    const VoxelState &sample = snapshot.voxels[static_cast<std::size_t>(index)];

    std::ostringstream out;
    out << "\x1b[H";
    out << section_bar(" EXPERIMENT 004 :: VOXEL INFRASTRUCTURE SOUNDING ", panel_width + 44, {248, 242, 226}, {22, 46, 70}) << "\n";
    out << color_text(" scenario ", {126, 198, 248})
        << "a marsh settlement sits above a buried conduit, delay basin, and collector that no one fully understands\n\n";

    draw_slice(out, snapshot, ui);

    std::vector<std::string> info_lines;
    info_lines.push_back(section_bar("STATUS", panel_width, {238, 238, 224}, {52, 70, 46}));
    info_lines.push_back(
        std::string(" ") + color_text("Tick", {132, 178, 236}) + " " + std::to_string(snapshot.tick) +
        "    " + color_text("View", {132, 178, 236}) + " " + view_name(ui.view) +
        "    " + color_text("Plane", {132, 178, 236}) + " " + std::to_string(active_plane(ui)) +
        "    " + color_text("Overlay", {132, 178, 236}) + " " + overlay_name(ui.overlay) +
        "    " + color_text("Sim", {132, 178, 236}) + " " + badge(ui.running ? "RUNNING" : "PAUSED", {18, 22, 32}, ui.running ? RgbColor{112, 196, 118} : RgbColor{128, 128, 132}));
    info_lines.push_back(
        std::string(" ") + color_text("Hidden Depth", {132, 178, 236}) + " " + std::to_string(ui.hidden_depth) +
        "    " + color_text("Sample", {132, 178, 236}) + " (" + std::to_string(ui.sample_x) + "," + std::to_string(ui.sample_y) + "," + std::to_string(ui.sample_z) + ")");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("REGION METRICS", panel_width, {236, 246, 244}, {18, 86, 90}));
    info_lines.push_back(" upland head         " + format_fixed(snapshot.metrics.upland_head) + "    leak flux           " + format_fixed(snapshot.metrics.leak_flux));
    info_lines.push_back(" marsh depth         " + format_fixed(snapshot.metrics.marsh_depth) + "    orchard supply      " + format_fixed(snapshot.metrics.orchard_supply));
    info_lines.push_back(" settlement stable   " + format_fixed(snapshot.metrics.settlement_stability) + "    conduit integrity   " + format_fixed(snapshot.metrics.conduit_integrity));
    info_lines.push_back(" collector clear     " + format_fixed(snapshot.metrics.collector_clearance) + "    spirit whisper      " + format_fixed(snapshot.metrics.spirit_whisper));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("SAMPLE VOXEL", panel_width, {238, 246, 232}, {88, 74, 26}));
    info_lines.push_back(" position            (" + std::to_string(ui.sample_x) + "," + std::to_string(ui.sample_y) + "," + std::to_string(ui.sample_z) + ")" +
                         "    material            " + material_name(sample.material));
    info_lines.push_back(" saturation          " + format_fixed(sample.saturation) + "    pressure            " + format_fixed(sample.pressure));
    info_lines.push_back(" structural risk     " + format_fixed(sample.stress) + "    salinity            " + format_fixed(sample.salinity));
    info_lines.push_back(" hidden              " + std::string(sample.hidden && !sample.exposed ? "yes" : "no") +
                         "    exposed             " + std::string(sample.exposed ? "yes" : "no"));
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("INTERVENTIONS", panel_width, {246, 236, 246}, {108, 46, 92}));
    info_lines.push_back(" x excavate inspection shaft: reveals one vertical slice but weakens local footing");
    info_lines.push_back(" p pack terrace fracture: reduces marsh seepage but can leave downslope scarcity unresolved");
    info_lines.push_back(" c clear collector spillway: restores orchard flow, may reduce local wetland buffering");
    info_lines.push_back(" v vent ancestor well: reveals hidden structure faster, but increases shortcut-path pressure noise");
    info_lines.push_back(" z reset scenario");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("OVERLAYS", panel_width, {238, 242, 248}, {42, 56, 82}));
    info_lines.push_back(" 1 material       2 saturation       3 pressure       4 structural risk");
    info_lines.push_back(" ? means the voxel exists, but its true material is still concealed from the current survey");
    info_lines.push_back(" hidden voxels may be buried, unexcavated, or only indirectly inferred from surface symptoms");
    info_lines.push_back(" excavation, better slice choice, or borrowed guidance can reveal what the ? actually is");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("CAMERA", panel_width, {238, 242, 248}, {52, 64, 100}));
    info_lines.push_back(" 5 XY slice at z       6 YZ slice at x       7 XZ slice at y       8 crude isometric");
    info_lines.push_back(" [ and ] move the active slice plane");
    info_lines.push_back(" , and . peel or restore front layers relative to the current slice direction");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("CONTROLS", panel_width, {244, 242, 232}, {70, 70, 74}));
    info_lines.push_back(" sim        space play/pause   n single-step   -/+ speed");
    info_lines.push_back(" inspect    arrow keys move sample within the current slice/view");
    info_lines.push_back(" planes     [ previous slice plane   ] next slice plane");
    info_lines.push_back(" reveal     , peel more front layers   . restore peeled layers");
    info_lines.push_back(" actions    x excavate   p pack fracture   c clear collector   v vent well   z reset");
    info_lines.push_back(" shell      q quit");
    info_lines.push_back(std::string());

    info_lines.push_back(section_bar("RECENT EVENTS", panel_width, {248, 242, 224}, {74, 74, 74}));
    if (snapshot.recent_events.empty())
    {
        info_lines.push_back(" no anomalies logged");
    }
    else
    {
        for (const std::string &event_text : snapshot.recent_events)
        {
            info_lines.push_back(" " + event_text);
        }
    }

    append_boxed_lines(
        out,
        ansi_bg({18, 24, 36}) + ansi_fg({232, 240, 248}) + pad_right(" STATUS / DIAGNOSTICS ", panel_width) + reset_ansi(),
        info_lines,
        panel_width);

    out << "\x1b[J";
    std::cout << out.str() << std::flush;
}

void handle_keypress(int key, Simulator &simulator, UiState &ui, const SimulationSnapshot &snapshot)
{
    ui.needs_redraw = true;

    if (key == 0 || key == 224)
    {
        const int extended = _getch();
        switch (extended)
        {
        case 72: move_sample(ui, snapshot, 0, 1); return;
        case 80: move_sample(ui, snapshot, 0, -1); return;
        case 75: move_sample(ui, snapshot, -1, 0); return;
        case 77: move_sample(ui, snapshot, 1, 0); return;
        default: return;
        }
    }

    switch (key)
    {
    case 'q':
    case 'Q':
        ui.quit_requested = true;
        return;
    case ' ':
        ui.running = !ui.running;
        return;
    case 'n':
    case 'N':
        simulator.step();
        return;
    case '1':
        ui.overlay = OverlayMode::Material;
        return;
    case '2':
        ui.overlay = OverlayMode::Saturation;
        return;
    case '3':
        ui.overlay = OverlayMode::Pressure;
        return;
    case '4':
        ui.overlay = OverlayMode::StructuralRisk;
        return;
    case '5':
        ui.view = ViewMode::XYSliceAtZ;
        sync_sample_to_view(ui, snapshot);
        return;
    case '6':
        ui.view = ViewMode::YZSliceAtX;
        sync_sample_to_view(ui, snapshot);
        return;
    case '7':
        ui.view = ViewMode::XZSliceAtY;
        sync_sample_to_view(ui, snapshot);
        return;
    case '8':
        ui.view = ViewMode::Isometric;
        return;
    case '[':
        step_active_plane(ui, snapshot, -1);
        sync_sample_to_view(ui, snapshot);
        return;
    case ']':
        step_active_plane(ui, snapshot, 1);
        sync_sample_to_view(ui, snapshot);
        return;
    case ',':
    case '<':
        switch (ui.view)
        {
        case ViewMode::XYSliceAtZ: ui.hidden_depth = std::min(snapshot.depth - 1, ui.hidden_depth + 1); break;
        case ViewMode::YZSliceAtX: ui.hidden_depth = std::min(snapshot.width - 1, ui.hidden_depth + 1); break;
        case ViewMode::XZSliceAtY: ui.hidden_depth = std::min(snapshot.height - 1, ui.hidden_depth + 1); break;
        case ViewMode::Isometric: ui.hidden_depth = std::min(snapshot.width + snapshot.height + snapshot.depth, ui.hidden_depth + 1); break;
        }
        return;
    case '.':
    case '>':
        ui.hidden_depth = std::max(0, ui.hidden_depth - 1);
        return;
    case '-':
        ui.tick_ms = std::min(1000, ui.tick_ms + 20);
        return;
    case '+':
    case '=':
        ui.tick_ms = std::max(20, ui.tick_ms - 20);
        return;
    case 'x':
    case 'X':
        simulator.apply_intervention(InterventionType::ExcavateInspectionShaft);
        return;
    case 'p':
    case 'P':
        simulator.apply_intervention(InterventionType::PackTerraceFracture);
        return;
    case 'c':
    case 'C':
        simulator.apply_intervention(InterventionType::ClearCollectorSpillway);
        return;
    case 'v':
    case 'V':
        simulator.apply_intervention(InterventionType::VentAncestorWell);
        return;
    case 'z':
    case 'Z':
        simulator.reset();
        return;
    default:
        return;
    }
}

int run_non_interactive(const DashboardOptions &options)
{
    Simulator simulator;
    UiState ui;
    ui.tick_ms = options.sleep_ms;
    sync_sample_to_view(ui, simulator.snapshot());

    for (int frame = 0; frame < options.ticks; ++frame)
    {
        if (options.clear_screen)
        {
            std::cout << "\x1B[2J\x1B[H";
        }

        draw_frame(simulator.snapshot(), ui);

        if (frame + 1 < options.ticks)
        {
            simulator.step();
        }

        if (options.sleep_ms > 0 && frame + 1 < options.ticks)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
        }
    }

    return 0;
}
} // namespace

int run_dashboard(const DashboardOptions &options)
{
    if (!options.interactive)
    {
        return run_non_interactive(options);
    }

    AlternateScreenSession terminal_session;
    Simulator simulator;
    UiState ui;
    ui.tick_ms = options.sleep_ms;
    sync_sample_to_view(ui, simulator.snapshot());

    auto next_tick = std::chrono::steady_clock::now();

    while (!ui.quit_requested)
    {
        SimulationSnapshot snapshot = simulator.snapshot();

        while (_kbhit())
        {
            handle_keypress(_getch(), simulator, ui, snapshot);
            snapshot = simulator.snapshot();
        }

        const auto now = std::chrono::steady_clock::now();
        if (ui.running && now >= next_tick)
        {
            simulator.step();
            ui.needs_redraw = true;
            next_tick = now + std::chrono::milliseconds(ui.tick_ms);
            snapshot = simulator.snapshot();
        }

        if (ui.needs_redraw)
        {
            draw_frame(snapshot, ui);
            ui.needs_redraw = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
} // namespace voxel_infrastructure_004
