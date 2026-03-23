#include "ui.h"

#include "simulation.h"
#include "terrain.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace scalar_field_flooding
{
namespace
{
struct RgbColor
{
    int red = 0;
    int green = 0;
    int blue = 0;
};

[[nodiscard]] RgbColor terrain_color(TerrainType terrain)
{
    switch (terrain)
    {
    case TerrainType::Bedrock:
        return {.red = 100, .green = 108, .blue = 122};
    case TerrainType::Clay:
        return {.red = 150, .green = 92, .blue = 74};
    case TerrainType::Loam:
        return {.red = 101, .green = 126, .blue = 76};
    case TerrainType::Sand:
        return {.red = 198, .green = 171, .blue = 104};
    }

    return {.red = 80, .green = 80, .blue = 80};
}

[[nodiscard]] RgbColor saturation_blue()
{
    return {.red = 34, .green = 120, .blue = 255};
}

[[nodiscard]] RgbColor blend_colors(RgbColor start, RgbColor finish, float t)
{
    const float clamped = clamp01(t);
    const auto blend_channel = [clamped](int a, int b) {
        return static_cast<int>(std::round(static_cast<float>(a) + (static_cast<float>(b - a) * clamped)));
    };

    return {
        .red = blend_channel(start.red, finish.red),
        .green = blend_channel(start.green, finish.green),
        .blue = blend_channel(start.blue, finish.blue),
    };
}

[[nodiscard]] std::string truecolor_background_ansi(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[48;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

[[nodiscard]] std::string truecolor_foreground_ansi(RgbColor color)
{
    std::ostringstream out;
    out << "\x1b[38;2;" << color.red << ';' << color.green << ';' << color.blue << 'm';
    return out.str();
}

[[nodiscard]] const char* ansi_reset()
{
    return "\x1b[0m";
}

[[nodiscard]] std::string rgb_css(RgbColor color)
{
    std::ostringstream out;
    out << "rgb(" << color.red << ", " << color.green << ", " << color.blue << ")";
    return out.str();
}

[[nodiscard]] RgbColor current_tile_color(const CellState& cell)
{
    const TerrainProperties properties = terrain_properties(cell.terrain);
    const float saturation_ratio =
        properties.saturation_capacity > 0.0F ? (cell.saturation / properties.saturation_capacity) : 0.0F;
    return blend_colors(terrain_color(cell.terrain), saturation_blue(), saturation_ratio);
}

[[nodiscard]] float relative_luminance(RgbColor color)
{
    return (0.2126F * static_cast<float>(color.red) +
            0.7152F * static_cast<float>(color.green) +
            0.0722F * static_cast<float>(color.blue)) / 255.0F;
}

[[nodiscard]] RgbColor readable_text_color(RgbColor background)
{
    // Use a light/dark foreground based on luminance so the console numbers
    // stay legible as the background moves through a continuous saturation ramp.
    if (relative_luminance(background) > 0.55F)
    {
        return {.red = 18, .green = 24, .blue = 33};
    }

    return {.red = 245, .green = 248, .blue = 252};
}

[[nodiscard]] std::string render_saturation_grid(const GridState& grid)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);

    for (int y = 0; y < grid.height(); ++y)
    {
        for (int x = 0; x < grid.width(); ++x)
        {
            const CellState& cell = grid.at(x, y);
            const RgbColor background = current_tile_color(cell);
            const RgbColor foreground = readable_text_color(background);

            out << truecolor_background_ansi(background);
            out << truecolor_foreground_ansi(foreground);
            out << std::setw(5) << cell.saturation;
            out << (cell.water_feature == WaterFeatureType::SpringSource ? '*' : ' ');
            out << ansi_reset();
        }
        out << '\n';
    }

    return out.str();
}
} // namespace

void write_html_snapshot(const GridState& grid, int step, const std::filesystem::path& output_path)
{
    std::filesystem::create_directories(output_path.parent_path());

    std::ofstream out(output_path, std::ios::trunc);
    if (!out)
    {
        throw std::runtime_error("Failed to open HTML export file: " + output_path.string());
    }

    const GridSummary summary = summarize_grid(grid);

    out << "<!doctype html>\n";
    out << "<html lang=\"en\">\n";
    out << "<head>\n";
    out << "  <meta charset=\"utf-8\">\n";
    out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out << "  <title>Scalar Field Flooding Step " << step << "</title>\n";
    out << "  <style>\n";
    out << "    :root { color-scheme: dark; }\n";
    out << "    body { margin: 0; padding: 32px; background: #0b1320; color: #e9f2ff; font: 16px/1.4 'Segoe UI', Arial, sans-serif; }\n";
    out << "    h1, h2, p { margin: 0 0 12px; }\n";
    out << "    .panel { background: #10203a; border: 1px solid #294261; border-radius: 18px; padding: 20px; margin-bottom: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.25); }\n";
    out << "    .legend-row { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 10px; }\n";
    out << "    .chip { display: inline-flex; align-items: center; gap: 10px; background: #132846; border: 1px solid #355780; border-radius: 999px; padding: 8px 12px; }\n";
    out << "    .swatch { width: 24px; height: 24px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.18); box-sizing: border-box; }\n";
    out << "    .grid { display: grid; grid-template-columns: repeat(" << grid.width() << ", 22px); gap: 3px; width: max-content; }\n";
    out << "    .cell { width: 22px; height: 22px; border-radius: 6px; position: relative; box-shadow: inset 0 0 0 1px rgba(255,255,255,0.08); }\n";
    out << "    .spring::after { content: ''; position: absolute; width: 8px; height: 8px; border-radius: 999px; background: #f4fbff; top: 7px; left: 7px; box-shadow: 0 0 8px rgba(255,255,255,0.7); }\n";
    out << "    .summary { color: #bfd5f0; }\n";
    out << "    code { color: #d8e8ff; }\n";
    out << "  </style>\n";
    out << "</head>\n";
    out << "<body>\n";
    out << "  <div class=\"panel\">\n";
    out << "    <h1>Scalar Field Flooding Snapshot</h1>\n";
    out << "    <p>Step " << step << "</p>\n";
    out << "    <p class=\"summary\">wet=" << summary.wet_cells
        << " airable=" << summary.airable_cells
        << " avg_sat=" << std::fixed << std::setprecision(2) << summary.average_saturation
        << " avg_air=" << summary.average_airability << "</p>\n";
    out << "  </div>\n";
    out << "  <div class=\"panel\">\n";
    out << "    <h2>Legend</h2>\n";
    out << "    <p>Tile color blends from terrain identity toward blue as each terrain approaches its own saturation capacity. Spring sources are marked with a white dot.</p>\n";
    out << "    <div class=\"legend-row\">\n";
    static constexpr TerrainType terrains[] = {
        TerrainType::Bedrock,
        TerrainType::Clay,
        TerrainType::Loam,
        TerrainType::Sand,
    };
    for (TerrainType terrain : terrains)
    {
        out << "      <div class=\"chip\"><span class=\"swatch\" style=\"background:" << rgb_css(terrain_color(terrain))
            << ";\"></span><span>" << terrain_name(terrain) << "</span></div>\n";
    }
    out << "      <div class=\"chip\"><span class=\"swatch\" style=\"background:" << rgb_css(saturation_blue())
        << ";\"></span><span>Fully saturated blue</span></div>\n";
    out << "    </div>\n";
    out << "  </div>\n";
    out << "  <div class=\"panel\">\n";
    out << "    <h2>Grid</h2>\n";
    out << "    <div class=\"grid\">\n";
    for (int y = 0; y < grid.height(); ++y)
    {
        for (int x = 0; x < grid.width(); ++x)
        {
            const CellState& cell = grid.at(x, y);
            out << "      <div class=\"cell";
            if (cell.water_feature == WaterFeatureType::SpringSource)
            {
                out << " spring";
            }
            out << "\" title=\""
                << terrain_name(cell.terrain)
                << " | saturation=" << std::fixed << std::setprecision(3) << cell.saturation
                << " | airability=" << current_airability(cell)
                << "\" style=\"background:" << rgb_css(current_tile_color(cell)) << ";\"></div>\n";
        }
    }
    out << "    </div>\n";
    out << "  </div>\n";
    out << "</body>\n";
    out << "</html>\n";
}

void write_bmp_snapshot(const GridState& grid, const std::filesystem::path& output_path)
{
    std::filesystem::create_directories(output_path.parent_path());

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        throw std::runtime_error("Failed to open BMP export file: " + output_path.string());
    }

    const int width = grid.width();
    const int height = grid.height();
    const int bytes_per_pixel = 3;
    const int row_stride = width * bytes_per_pixel;
    const int row_padding = (4 - (row_stride % 4)) % 4;
    const int pixel_data_size = (row_stride + row_padding) * height;
    const int file_size = 14 + 40 + pixel_data_size;

    const auto write_u16 = [&out](std::uint16_t value) {
        out.put(static_cast<char>(value & 0xFF));
        out.put(static_cast<char>((value >> 8) & 0xFF));
    };
    const auto write_u32 = [&out](std::uint32_t value) {
        out.put(static_cast<char>(value & 0xFF));
        out.put(static_cast<char>((value >> 8) & 0xFF));
        out.put(static_cast<char>((value >> 16) & 0xFF));
        out.put(static_cast<char>((value >> 24) & 0xFF));
    };

    write_u16(0x4D42);
    write_u32(static_cast<std::uint32_t>(file_size));
    write_u16(0);
    write_u16(0);
    write_u32(54);

    write_u32(40);
    write_u32(static_cast<std::uint32_t>(width));
    write_u32(static_cast<std::uint32_t>(height));
    write_u16(1);
    write_u16(24);
    write_u32(0);
    write_u32(static_cast<std::uint32_t>(pixel_data_size));
    write_u32(2835);
    write_u32(2835);
    write_u32(0);
    write_u32(0);

    const char padding[3] = {0, 0, 0};
    for (int y = height - 1; y >= 0; --y)
    {
        for (int x = 0; x < width; ++x)
        {
            const CellState& cell = grid.at(x, y);
            RgbColor color = current_tile_color(cell);
            if (cell.water_feature == WaterFeatureType::SpringSource)
            {
                color = {.red = 245, .green = 250, .blue = 255};
            }

            out.put(static_cast<char>(color.blue));
            out.put(static_cast<char>(color.green));
            out.put(static_cast<char>(color.red));
        }

        out.write(padding, row_padding);
    }

    if (!out)
    {
        throw std::runtime_error("Failed while writing BMP export file: " + output_path.string());
    }
}

void print_terrain_coefficients()
{
    static constexpr TerrainType terrains[] = {
        TerrainType::Bedrock,
        TerrainType::Clay,
        TerrainType::Loam,
        TerrainType::Sand,
    };

    std::cout << "Terrain coefficients:\n";
    std::cout << "  Name      Capacity Spread  AirBase\n";

    for (TerrainType terrain : terrains)
    {
        const TerrainProperties properties = terrain_properties(terrain);
        std::cout << "  " << std::left << std::setw(8) << terrain_name(terrain)
                  << "  " << std::setw(8) << properties.saturation_capacity
                  << "  " << std::setw(6) << properties.lateral_spread
                  << "  " << terrain_airability_potential(terrain) << '\n';
    }

    std::cout << '\n';
}

void print_step_report(const GridState& grid, int step)
{
    const GridSummary summary = summarize_grid(grid);

    std::cout << "Step " << step << " report\n";
    std::cout << "  Legend:\n";
    std::cout << "    terrain dry colors   "
              << truecolor_background_ansi(terrain_color(TerrainType::Bedrock)) << "   " << ansi_reset() << " Bedrock  "
              << truecolor_background_ansi(terrain_color(TerrainType::Clay)) << "   " << ansi_reset() << " Clay  "
              << truecolor_background_ansi(terrain_color(TerrainType::Loam)) << "   " << ansi_reset() << " Loam  "
              << truecolor_background_ansi(terrain_color(TerrainType::Sand)) << "   " << ansi_reset() << " Sand\n";
    std::cout << "    saturation blend     "
              << truecolor_background_ansi(blend_colors(terrain_color(TerrainType::Loam), saturation_blue(), 0.15F)) << "   " << ansi_reset() << " low  "
              << truecolor_background_ansi(blend_colors(terrain_color(TerrainType::Loam), saturation_blue(), 0.50F)) << "   " << ansi_reset() << " medium  "
              << truecolor_background_ansi(blend_colors(terrain_color(TerrainType::Loam), saturation_blue(), 0.85F)) << "   " << ansi_reset() << " high  "
              << truecolor_background_ansi(saturation_blue()) << "\x1b[38;2;245;250;255m o " << ansi_reset() << " spring source\n";
    std::cout << "  Summary: wet=" << summary.wet_cells
              << " airable=" << summary.airable_cells
              << " avg_sat=" << std::fixed << std::setprecision(2) << summary.average_saturation
              << " avg_air=" << summary.average_airability << "\n\n";
    if (grid.width() <= 120 && grid.height() <= 60)
    {
        std::cout << "  Saturation grid (`*` marks a spring cell):\n";
        std::cout << render_saturation_grid(grid) << '\n';
    }
    else
    {
        std::cout << "  Saturation grid omitted from terminal for large grids (" << grid.width() << 'x' << grid.height() << ").\n\n";
    }
}
} // namespace scalar_field_flooding
