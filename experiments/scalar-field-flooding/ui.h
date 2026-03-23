#pragma once

#include <filesystem>

namespace scalar_field_flooding
{
class GridState;

void print_terrain_coefficients();
void print_step_report(const GridState& grid, int step);
void write_html_snapshot(const GridState& grid, int step, const std::filesystem::path& output_path);
void write_bmp_snapshot(const GridState& grid, const std::filesystem::path& output_path);
} // namespace scalar_field_flooding
