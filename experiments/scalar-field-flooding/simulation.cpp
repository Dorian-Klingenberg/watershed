#include "simulation.h"

#include "terrain.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace scalar_field_flooding
{
namespace
{
constexpr int k_neighbor_offsets[4][2] = {
    {1, 0},
    {-1, 0},
    {0, 1},
    {0, -1},
};

[[nodiscard]] const CellState& boundary_granite_cell()
{
    static const CellState boundary = {
        .terrain = TerrainType::Bedrock,
        .water_feature = WaterFeatureType::None,
        .saturation = 0.0F,
    };
    return boundary;
}

[[nodiscard]] const CellState& neighbor_or_granite_boundary(const GridState& grid, int x, int y)
{
    if (!grid.in_bounds(x, y))
    {
        return boundary_granite_cell();
    }

    return grid.at(x, y);
}

[[nodiscard]] bool has_adjacent_spring(const GridState& grid, int x, int y)
{
    for (const auto& offset : k_neighbor_offsets)
    {
        const int neighbor_x = x + offset[0];
        const int neighbor_y = y + offset[1];
        if (!grid.in_bounds(neighbor_x, neighbor_y))
        {
            continue;
        }

        if (grid.at(neighbor_x, neighbor_y).water_feature == WaterFeatureType::SpringSource)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::size_t checked_inflow_index(const GridState& grid, int x, int y, const char* label)
{
    if (!grid.in_bounds(x, y))
    {
        std::ostringstream message;
        message << label << " index out of bounds for (" << x << ", " << y << ") on "
                << grid.width() << "x" << grid.height() << " grid.";
        throw std::out_of_range(message.str());
    }

    return static_cast<std::size_t>(y * grid.width() + x);
}

void require_finite(float value, const char* label, int x, int y)
{
    if (std::isfinite(value))
    {
        return;
    }

    std::ostringstream message;
    message << label << " became non-finite at (" << x << ", " << y << "): " << value;
    throw std::runtime_error(message.str());
}
} // namespace

GridState::GridState(int width, int height)
    : width_(width), height_(height), cells_(static_cast<std::size_t>(width * height))
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Grid dimensions must be positive.");
    }
}

int GridState::width() const
{
    return width_;
}

int GridState::height() const
{
    return height_;
}

bool GridState::in_bounds(int x, int y) const
{
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

CellState& GridState::at(int x, int y)
{
    return cells_.at(index_for(x, y));
}

const CellState& GridState::at(int x, int y) const
{
    return cells_.at(index_for(x, y));
}

std::size_t GridState::index_for(int x, int y) const
{
    if (!in_bounds(x, y))
    {
        throw std::out_of_range("Grid index out of bounds.");
    }

    return static_cast<std::size_t>(y * width_ + x);
}

float clamp01(float value)
{
    return value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
}

float current_airability(const CellState& cell)
{
    const TerrainProperties properties = terrain_properties(cell.terrain);
    const float base_airability = terrain_airability_potential(cell.terrain);
    const float saturation_fraction = cell.saturation / properties.saturation_capacity;
    return clamp01(base_airability * (1.0F - saturation_fraction));
}

bool is_airable(const CellState& cell)
{
    const float airability = current_airability(cell);
    return airability >= 0.20F && airability <= 0.65F;
}

GridSummary summarize_grid(const GridState& grid)
{
    GridSummary summary;
    const int total_cells = grid.width() * grid.height();

    for (int y = 0; y < grid.height(); ++y)
    {
        for (int x = 0; x < grid.width(); ++x)
        {
            const CellState& cell = grid.at(x, y);
            const float airability = current_airability(cell);

            if (cell.saturation > 0.05F)
            {
                ++summary.wet_cells;
            }

            if (is_airable(cell))
            {
                ++summary.airable_cells;
            }

            summary.average_saturation += cell.saturation;
            summary.average_airability += airability;
        }
    }

    if (total_cells > 0)
    {
        summary.average_saturation /= static_cast<float>(total_cells);
        summary.average_airability /= static_cast<float>(total_cells);
    }

    return summary;
}
GridState simulate_step(const GridState& current)
{
    GridState next = current;
    std::vector<float> lateral_delta(
        static_cast<std::size_t>(current.width() * current.height()), 0.0F);
    std::vector<float> downhill_inflow(
        static_cast<std::size_t>(current.width() * current.height()), 0.0F);

    // One lateral transfer pass moves water pressure sideways based on the
    // current step's state, including granite-like dry boundaries off-map.
    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const CellState& cell = current.at(x, y);
            const TerrainProperties properties = terrain_properties(cell.terrain);

            for (const auto& offset : k_neighbor_offsets)
            {
                const int nx = x + offset[0];
                const int ny = y + offset[1];
                const CellState& neighbor = neighbor_or_granite_boundary(current, nx, ny);
                const TerrainProperties neighbor_properties = terrain_properties(neighbor.terrain);
                const float gradient = neighbor.saturation - cell.saturation;
                const float spread_strength =
                    (properties.lateral_spread + neighbor_properties.lateral_spread) * 0.5F;
                const std::size_t index = checked_inflow_index(current, x, y, "lateral_delta");

                require_finite(gradient, "gradient", x, y);
                require_finite(spread_strength, "spread_strength", x, y);
                lateral_delta[index] += gradient * spread_strength * 0.25F;
                require_finite(lateral_delta[index], "lateral_delta", x, y);
            }
        }
    }

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const CellState& cell = current.at(x, y);
            const TerrainProperties properties = terrain_properties(cell.terrain);
            const std::size_t index = checked_inflow_index(current, x, y, "cell");

            require_finite(cell.saturation, "cell.saturation", x, y);
            require_finite(lateral_delta[index], "lateral_delta before saturation update", x, y);

            float next_saturation = cell.saturation + lateral_delta[index];
            require_finite(next_saturation, "next_saturation after lateral pass", x, y);

            if (cell.water_feature == WaterFeatureType::SpringSource)
            {
                next_saturation += 0.30F;
                require_finite(next_saturation, "next_saturation after spring source", x, y);
            }

            if (has_adjacent_spring(current, x, y))
            {
                next_saturation += 0.12F;
                require_finite(next_saturation, "next_saturation after adjacent spring", x, y);
            }

            // Once a cell exceeds its terrain capacity, the excess runs
            // downhill. Higher-capacity terrains accumulate more before this
            // happens; low-capacity ones shed earlier.
            const float overflow = std::max(0.0F, next_saturation - properties.saturation_capacity);
            require_finite(overflow, "overflow", x, y);
            next_saturation -= overflow;
            require_finite(next_saturation, "next_saturation after overflow", x, y);
            next.at(x, y).saturation = std::max(0.0F, next_saturation);

            const int below_y = y + 1;
            if (current.in_bounds(x, below_y))
            {
                const std::size_t below_index = checked_inflow_index(current, x, below_y, "downhill_inflow");
                downhill_inflow[below_index] += overflow;
                require_finite(downhill_inflow[below_index], "downhill_inflow", x, below_y);
            }
        }
    }

    for (int y = 0; y < current.height(); ++y)
    {
        for (int x = 0; x < current.width(); ++x)
        {
            const std::size_t index = checked_inflow_index(current, x, y, "final pass");
            require_finite(downhill_inflow[index], "downhill_inflow before final pass", x, y);
            next.at(x, y).saturation = std::max(0.0F, next.at(x, y).saturation + downhill_inflow[index]);
            require_finite(next.at(x, y).saturation, "final saturation", x, y);
        }
    }

    return next;
}
} // namespace scalar_field_flooding
