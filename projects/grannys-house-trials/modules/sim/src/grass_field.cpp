#include "grannys_house_trials/sim/grass_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace grannys_house_trials::sim
{
namespace
{
constexpr int inches_per_foot = 12;

[[nodiscard]] float fade(float value) noexcept
{
    return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] std::uint32_t hash_2d(int x, int z) noexcept
{
    std::uint32_t value = static_cast<std::uint32_t>(x) * 0x27d4eb2dU;
    value ^= static_cast<std::uint32_t>(z) * 0x85ebca6bU;
    value ^= value >> 15;
    value *= 0x2c1b3c6dU;
    value ^= value >> 12;
    value *= 0x297a2d39U;
    value ^= value >> 15;
    return value;
}

[[nodiscard]] float gradient_dot(int lattice_x, int lattice_z, float delta_x, float delta_z) noexcept
{
    switch (hash_2d(lattice_x, lattice_z) & 7U)
    {
    case 0:
        return delta_x + delta_z;
    case 1:
        return delta_x - delta_z;
    case 2:
        return -delta_x + delta_z;
    case 3:
        return -delta_x - delta_z;
    case 4:
        return delta_x;
    case 5:
        return -delta_x;
    case 6:
        return delta_z;
    default:
        return -delta_z;
    }
}

[[nodiscard]] float perlin_noise_2d(float x, float z) noexcept
{
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = x - static_cast<float>(x0);
    const float tz = z - static_cast<float>(z0);

    const float n00 = gradient_dot(x0, z0, tx, tz);
    const float n10 = gradient_dot(x1, z0, tx - 1.0f, tz);
    const float n01 = gradient_dot(x0, z1, tx, tz - 1.0f);
    const float n11 = gradient_dot(x1, z1, tx - 1.0f, tz - 1.0f);

    const float u = fade(tx);
    const float v = fade(tz);

    return lerp(
        lerp(n00, n10, u),
        lerp(n01, n11, u),
        v);
}

[[nodiscard]] int perlin_height_voxels_for(int x, int z) noexcept
{
    const float sample_x = static_cast<float>(x) / 18.0f;
    const float sample_z = static_cast<float>(z) / 18.0f;

    const float octave_a = perlin_noise_2d(sample_x, sample_z);
    const float octave_b = perlin_noise_2d(sample_x * 2.1f + 17.3f, sample_z * 2.1f - 9.7f);
    const float octave_c = perlin_noise_2d(sample_x * 4.3f - 31.1f, sample_z * 4.3f + 11.4f);

    const float combined_noise = octave_a * 0.58f + octave_b * 0.29f + octave_c * 0.13f;
    const float normalized = std::clamp(combined_noise * 1.35f + 0.5f, 0.0f, 0.9999f);
    return 1 + static_cast<int>(normalized * 5.0f);
}

[[nodiscard]] bool is_homestead_pad_cell(int x, int z) noexcept
{
    return x >= 60 && x <= 72 && z >= 46 && z <= 56;
}

[[nodiscard]] bool is_on_rect_perimeter(
    int x,
    int z,
    int min_x,
    int max_x,
    int min_z,
    int max_z) noexcept
{
    if (x < min_x || x > max_x || z < min_z || z > max_z)
    {
        return false;
    }

    return x == min_x || x == max_x || z == min_z || z == max_z;
}

[[nodiscard]] int terrace_level_for(int x, int z) noexcept
{
    if (x >= 76 && x <= 92 && z >= 36 && z <= 42)
    {
        return 3;
    }

    if (x >= 78 && x <= 90 && z >= 43 && z <= 48)
    {
        return 2;
    }

    if (x >= 80 && x <= 88 && z >= 49 && z <= 54)
    {
        return 1;
    }

    return 0;
}

[[nodiscard]] bool is_garden_bed_cell(int x, int z) noexcept
{
    return terrace_level_for(x, z) > 0;
}

[[nodiscard]] bool is_pool_water_cell(int x, int z) noexcept
{
    return x >= 88 && x <= 91 && z >= 37 && z <= 40;
}

[[nodiscard]] bool is_pool_rim_cell(int x, int z) noexcept
{
    return is_on_rect_perimeter(x, z, 87, 92, 36, 41) && !is_pool_water_cell(x, z);
}

[[nodiscard]] bool is_terrace_retaining_cell(int x, int z) noexcept
{
    if (is_pool_water_cell(x, z) || is_pool_rim_cell(x, z))
    {
        return false;
    }

    return is_on_rect_perimeter(x, z, 76, 92, 36, 42)
        || is_on_rect_perimeter(x, z, 78, 90, 43, 48)
        || is_on_rect_perimeter(x, z, 80, 88, 49, 54);
}

[[nodiscard]] int pool_center_distance_squared(int local_x_inches, int local_z_inches) noexcept
{
    const int centered_x = local_x_inches * 2 - 11;
    const int centered_z = local_z_inches * 2 - 11;
    return centered_x * centered_x + centered_z * centered_z;
}

[[nodiscard]] bool is_path_stone_cell(int x, int z) noexcept
{
    return x >= 50 && x <= 88 && z >= 60 && z <= 62;
}

[[nodiscard]] bool is_wet_patch_cell(int x, int z) noexcept
{
    return x >= 18 && x <= 28 && z >= 72 && z <= 82;
}

[[nodiscard]] bool is_ancient_brick_cell(int x, int z) noexcept
{
    return x >= 30 && x <= 35 && z >= 18 && z <= 23;
}

[[nodiscard]] TerrainMaterial material_for_cell(int x, int z) noexcept
{
    if (is_pool_water_cell(x, z))
    {
        return TerrainMaterial::PoolWater;
    }

    if (is_pool_rim_cell(x, z))
    {
        return TerrainMaterial::AncientBrick;
    }

    if (is_terrace_retaining_cell(x, z))
    {
        return TerrainMaterial::AncientBrick;
    }

    if (is_garden_bed_cell(x, z))
    {
        return TerrainMaterial::GardenLoam;
    }

    if (is_homestead_pad_cell(x, z))
    {
        return TerrainMaterial::PackedEarth;
    }

    if (is_path_stone_cell(x, z))
    {
        return TerrainMaterial::PathStone;
    }

    if (is_wet_patch_cell(x, z))
    {
        return TerrainMaterial::WetSoil;
    }

    if (is_ancient_brick_cell(x, z))
    {
        return TerrainMaterial::AncientBrick;
    }

    return TerrainMaterial::Grass;
}

[[nodiscard]] GardenAttributes garden_attributes_for(const GrassVoxel &cell, int x, int z) noexcept
{
    GardenAttributes attributes{};
    attributes.soil_moisture = cell.column_height_voxels >= 3 ? 0.44f : 0.58f;
    attributes.fertility = 0.62f;
    attributes.sunlight = cell.column_height_voxels >= 3 ? 0.86f : 0.78f;
    attributes.weed_pressure = 0.34f;

    switch (cell.material)
    {
    case TerrainMaterial::Grass:
        break;
    case TerrainMaterial::GardenLoam:
        attributes.soil_moisture = 0.72f;
        attributes.fertility = 0.92f;
        attributes.sunlight = 0.88f;
        attributes.weed_pressure = 0.18f;
        attributes.is_garden_bed = true;
        break;
    case TerrainMaterial::PackedEarth:
        attributes.soil_moisture = 0.28f;
        attributes.fertility = 0.26f;
        attributes.sunlight = 0.82f;
        attributes.weed_pressure = 0.08f;
        break;
    case TerrainMaterial::PathStone:
        attributes.soil_moisture = 0.18f;
        attributes.fertility = 0.12f;
        attributes.sunlight = 0.84f;
        attributes.weed_pressure = 0.02f;
        break;
    case TerrainMaterial::WetSoil:
        attributes.soil_moisture = 0.88f;
        attributes.fertility = 0.54f;
        attributes.sunlight = 0.74f;
        attributes.weed_pressure = 0.46f;
        break;
    case TerrainMaterial::AncientBrick:
        attributes.soil_moisture = 0.22f;
        attributes.fertility = 0.08f;
        attributes.sunlight = 0.80f;
        attributes.weed_pressure = 0.06f;
        break;
    case TerrainMaterial::PoolWater:
        attributes.soil_moisture = 1.00f;
        attributes.fertility = 0.00f;
        attributes.sunlight = 0.82f;
        attributes.weed_pressure = 0.00f;
        break;
    }

    return attributes;
}

[[nodiscard]] DetailPatch detail_patch_for_cell(const GrassVoxel &cell, int x, int z) noexcept
{
    DetailPatch patch{};

    const auto set_offset = [&](int local_x_inches, int local_z_inches, int offset_inches) {
        patch.top_offset_inches[static_cast<std::size_t>(local_z_inches * DetailPatch::resolution + local_x_inches)]
            = static_cast<std::int8_t>(offset_inches);
        if (offset_inches != 0)
        {
            patch.is_active = true;
        }
    };

    if (cell.material == TerrainMaterial::GardenLoam)
    {
        for (int local_z_inches = 0; local_z_inches < DetailPatch::resolution; ++local_z_inches)
        {
            for (int local_x_inches = 0; local_x_inches < DetailPatch::resolution; ++local_x_inches)
            {
                const int furrow_phase = (local_z_inches + z * DetailPatch::resolution) % 4;
                int offset_inches = 0;

                if (furrow_phase == 0)
                {
                    offset_inches = -2;
                }
                else if (furrow_phase == 1)
                {
                    offset_inches = -1;
                }
                else if (furrow_phase == 3)
                {
                    offset_inches = 1;
                }

                if (local_x_inches == 0 || local_x_inches == DetailPatch::resolution - 1)
                {
                    offset_inches = std::max(offset_inches, 1);
                }

                set_offset(local_x_inches, local_z_inches, offset_inches);
            }
        }
    }

    if (cell.material == TerrainMaterial::PoolWater)
    {
        for (int local_z_inches = 0; local_z_inches < DetailPatch::resolution; ++local_z_inches)
        {
            for (int local_x_inches = 0; local_x_inches < DetailPatch::resolution; ++local_x_inches)
            {
                const int distance_squared = pool_center_distance_squared(local_x_inches, local_z_inches);
                int offset_inches = -2;

                if (distance_squared <= 18)
                {
                    offset_inches = -5;
                }
                else if (distance_squared <= 50)
                {
                    offset_inches = -4;
                }
                else if (distance_squared <= 98)
                {
                    offset_inches = -3;
                }

                set_offset(local_x_inches, local_z_inches, offset_inches);
            }
        }
    }

    if (cell.material == TerrainMaterial::AncientBrick
        && (is_pool_rim_cell(x, z) || is_terrace_retaining_cell(x, z)))
    {
        for (int local_z_inches = 0; local_z_inches < DetailPatch::resolution; ++local_z_inches)
        {
            for (int local_x_inches = 0; local_x_inches < DetailPatch::resolution; ++local_x_inches)
            {
                int offset_inches = 1;

                const bool on_patch_edge = local_x_inches == 0
                    || local_x_inches == DetailPatch::resolution - 1
                    || local_z_inches == 0
                    || local_z_inches == DetailPatch::resolution - 1;

                if (on_patch_edge)
                {
                    offset_inches = 2;
                }

                if (is_pool_rim_cell(x, z))
                {
                    const bool inner_rim_band = local_x_inches == 1
                        || local_x_inches == DetailPatch::resolution - 2
                        || local_z_inches == 1
                        || local_z_inches == DetailPatch::resolution - 2;
                    if (inner_rim_band)
                    {
                        offset_inches = 0;
                    }
                }

                set_offset(local_x_inches, local_z_inches, offset_inches);
            }
        }
    }

    return patch;
}
} // namespace

GrassField::GrassField(int width, int depth, float voxel_size_feet)
    : width_(width)
    , depth_(depth)
    , voxel_size_feet_(voxel_size_feet)
{
    if (width_ <= 0)
    {
        throw std::invalid_argument("GrassField width must be positive.");
    }

    if (depth_ <= 0)
    {
        throw std::invalid_argument("GrassField depth must be positive.");
    }

    if (voxel_size_feet_ <= 0.0f)
    {
        throw std::invalid_argument("GrassField voxel size must be positive.");
    }

    cells_.assign(static_cast<std::size_t>(width_ * depth_), GrassVoxel{});
    detail_patches_.assign(static_cast<std::size_t>(width_ * depth_), DetailPatch{});

    for (int z = 0; z < depth_; ++z)
    {
        for (int x = 0; x < width_; ++x)
        {
            const std::size_t cell_index = index_of(x, z);
            GrassVoxel &cell = cells_[cell_index];
            cell.material = material_for_cell(x, z);
            cell.column_height_voxels = perlin_height_voxels_for(x, z);

            if (is_homestead_pad_cell(x, z))
            {
                cell.column_height_voxels = 2;
                cell.is_homestead_pad = true;
            }

            const int terrace_level = terrace_level_for(x, z);
            if (terrace_level > 0)
            {
                cell.column_height_voxels = terrace_level + 1;
            }

            if (cell.material == TerrainMaterial::PathStone)
            {
                cell.column_height_voxels = 1;
            }

            if (cell.material == TerrainMaterial::WetSoil)
            {
                cell.column_height_voxels = 1;
            }

            if (cell.material == TerrainMaterial::AncientBrick)
            {
                cell.column_height_voxels = std::max(cell.column_height_voxels, 2);
            }

            if (cell.material == TerrainMaterial::PoolWater)
            {
                cell.column_height_voxels = 1;
            }

            cell.garden = garden_attributes_for(cell, x, z);

            DetailPatch &detail_patch = detail_patches_[cell_index];
            detail_patch = detail_patch_for_cell(cell, x, z);
            cell.has_detail_patch = detail_patch.is_active;
        }
    }
}

int DetailPatch::offset_at(int local_x_inches, int local_z_inches) const
{
    if (local_x_inches < 0 || local_x_inches >= resolution || local_z_inches < 0 || local_z_inches >= resolution)
    {
        throw std::out_of_range("DetailPatch coordinates are out of range.");
    }

    return static_cast<int>(top_offset_inches[static_cast<std::size_t>(local_z_inches * resolution + local_x_inches)]);
}

int GrassField::width() const noexcept
{
    return width_;
}

int GrassField::depth() const noexcept
{
    return depth_;
}

float GrassField::voxel_size_feet() const noexcept
{
    return voxel_size_feet_;
}

const GrassVoxel &GrassField::at(int x, int z) const
{
    return cells_.at(index_of(x, z));
}

bool GrassField::has_detail_patch(int x, int z) const
{
    return detail_patches_.at(index_of(x, z)).is_active;
}

const DetailPatch &GrassField::detail_patch_at(int x, int z) const
{
    return detail_patches_.at(index_of(x, z));
}

int GrassField::coarse_top_height_inches_at(int x, int z) const
{
    return at(x, z).column_height_voxels * inches_per_foot;
}

int GrassField::fine_top_height_inches_at(
    int x,
    int z,
    int local_x_inches,
    int local_z_inches) const
{
    const int coarse_top_height_inches = coarse_top_height_inches_at(x, z);
    const DetailPatch &detail_patch = detail_patch_at(x, z);

    if (!detail_patch.is_active)
    {
        return coarse_top_height_inches;
    }

    return coarse_top_height_inches + detail_patch.offset_at(local_x_inches, local_z_inches);
}

std::size_t GrassField::cell_count() const noexcept
{
    return cells_.size();
}

std::size_t GrassField::index_of(int x, int z) const
{
    if (x < 0 || x >= width_ || z < 0 || z >= depth_)
    {
        throw std::out_of_range("GrassField coordinates are out of range.");
    }

    return static_cast<std::size_t>(z * width_ + x);
}
} // namespace grannys_house_trials::sim
