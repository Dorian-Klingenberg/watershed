#include "map_generation.h"

#include "terrain.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scalar_field_flooding
{
namespace
{
struct NormalizedPoint
{
    float x = 0.0F;
    float y = 0.0F;
};
} // namespace

int normalized_coordinate(float fraction, int size)
{
    if (size <= 0)
    {
        throw std::invalid_argument("Normalized coordinates require a positive dimension.");
    }

    const int last_index = size - 1;
    return std::clamp(static_cast<int>(fraction * static_cast<float>(last_index)), 0, last_index);
}

int terrain_drift_offset(int x, int width, int height)
{
    if (width <= 1 || height <= 1)
    {
        return 0;
    }

    const float normalized_x = static_cast<float>(x) / static_cast<float>(width - 1);
    const float wave = std::sin(normalized_x * 6.2831853F * 1.5F);
    const float amplitude = std::max(1.0F, static_cast<float>(height) * 0.12F);
    return static_cast<int>(std::round(wave * amplitude));
}

std::vector<std::pair<int, int>> generated_spring_positions(int width, int height)
{
    static constexpr NormalizedPoint anchors[] = {
        {0.15F, 0.10F},
        {0.78F, 0.18F},
        // {0.25F, 0.36F},
        // {0.84F, 0.45F},
        // {0.18F, 0.62F},
        // {0.66F, 0.71F},
        {0.34F, 0.86F},
        {0.90F, 0.93F},
    };

    std::vector<std::pair<int, int>> positions;
    positions.reserve(std::size(anchors));

    for (const NormalizedPoint& anchor : anchors)
    {
        const int x = normalized_coordinate(anchor.x, width);
        const int y = normalized_coordinate(anchor.y, height);
        const std::pair<int, int> position{x, y};

        if (std::find(positions.begin(), positions.end(), position) == positions.end())
        {
            positions.push_back(position);
        }
    }

    return positions;
}

GridState make_manual_test_map()
{
    GridState grid(k_experiment_grid_width, k_experiment_grid_height);

    for (int y = 0; y < grid.height(); ++y)
    {
        for (int x = 0; x < grid.width(); ++x)
        {
            CellState& cell = grid.at(x, y);
            const int drifted_y = y + terrain_drift_offset(x, grid.width(), grid.height());
            if (drifted_y < grid.height() / 4)
            {
                cell.terrain = TerrainType::Bedrock;
            }
            else if (drifted_y < grid.height() / 2)
            {
                cell.terrain = TerrainType::Clay;
            }
            else if (drifted_y < (grid.height() * 3) / 4)
            {
                cell.terrain = TerrainType::Loam;
            }
            else
            {
                cell.terrain = TerrainType::Sand;
            }
            cell.saturation = 0.0F;
        }
    }

    for (const auto& spring : generated_spring_positions(grid.width(), grid.height()))
    {
        grid.at(spring.first, spring.second).water_feature = WaterFeatureType::SpringSource;
    }

    return grid;
}
} // namespace scalar_field_flooding
