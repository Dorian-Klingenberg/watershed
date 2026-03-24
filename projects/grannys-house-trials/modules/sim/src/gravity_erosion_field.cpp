#include "grannys_house_trials/sim/gravity_erosion_field.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace grannys_house_trials::sim
{
GravityErosionField::GravityErosionField(const GrassField &coarse_field)
    : coarse_width_(coarse_field.width())
    , coarse_depth_(coarse_field.depth())
    , coarse_top_heights_inches_(static_cast<std::size_t>(coarse_width_ * coarse_depth_), 0)
    , fine_surface_heights_inches_(static_cast<std::size_t>(fine_width() * fine_depth()), 0)
    , erodible_mask_(static_cast<std::size_t>(fine_width() * fine_depth()), 0)
{
    for (int coarse_z = 0; coarse_z < coarse_depth_; ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < coarse_width_; ++coarse_x)
        {
            const GrassVoxel &cell = coarse_field.at(coarse_x, coarse_z);
            const int coarse_top_height_inches = coarse_field.coarse_top_height_inches_at(coarse_x, coarse_z);
            coarse_top_heights_inches_[coarse_index_of(coarse_x, coarse_z)] = static_cast<std::int16_t>(coarse_top_height_inches);

            for (int local_z_inches = 0; local_z_inches < patch_resolution_; ++local_z_inches)
            {
                for (int local_x_inches = 0; local_x_inches < patch_resolution_; ++local_x_inches)
                {
                    const int fine_x = coarse_x * patch_resolution_ + local_x_inches;
                    const int fine_z = coarse_z * patch_resolution_ + local_z_inches;
                    fine_surface_heights_inches_[fine_index_of(fine_x, fine_z)] =
                        static_cast<std::int16_t>(coarse_field.fine_top_height_inches_at(
                            coarse_x,
                            coarse_z,
                            local_x_inches,
                            local_z_inches));
                    erodible_mask_[fine_index_of(fine_x, fine_z)] =
                        is_erodible_material(cell.material) ? 1u : 0u;
                }
            }
        }
    }
}

int GravityErosionField::coarse_width() const noexcept
{
    return coarse_width_;
}

int GravityErosionField::coarse_depth() const noexcept
{
    return coarse_depth_;
}

int GravityErosionField::patch_resolution() const noexcept
{
    return patch_resolution_;
}

int GravityErosionField::cycle_count() const noexcept
{
    return cycle_count_;
}

int GravityErosionField::coarse_top_height_inches_at(int coarse_x, int coarse_z) const
{
    return coarse_top_heights_inches_.at(coarse_index_of(coarse_x, coarse_z));
}

int GravityErosionField::fine_top_height_inches_at(
    int coarse_x,
    int coarse_z,
    int local_x_inches,
    int local_z_inches) const
{
    if (local_x_inches < 0 || local_x_inches >= patch_resolution_ || local_z_inches < 0 || local_z_inches >= patch_resolution_)
    {
        throw std::out_of_range("GravityErosionField local coordinates are out of range.");
    }

    const int fine_x = coarse_x * patch_resolution_ + local_x_inches;
    const int fine_z = coarse_z * patch_resolution_ + local_z_inches;
    return fine_height_at_global(fine_x, fine_z);
}

int GravityErosionField::patch_min_height_inches_at(int coarse_x, int coarse_z) const
{
    int minimum_height = fine_top_height_inches_at(coarse_x, coarse_z, 0, 0);

    for (int local_z_inches = 0; local_z_inches < patch_resolution_; ++local_z_inches)
    {
        for (int local_x_inches = 0; local_x_inches < patch_resolution_; ++local_x_inches)
        {
            minimum_height = std::min(
                minimum_height,
                fine_top_height_inches_at(coarse_x, coarse_z, local_x_inches, local_z_inches));
        }
    }

    return minimum_height;
}

int GravityErosionField::patch_max_height_inches_at(int coarse_x, int coarse_z) const
{
    int maximum_height = fine_top_height_inches_at(coarse_x, coarse_z, 0, 0);

    for (int local_z_inches = 0; local_z_inches < patch_resolution_; ++local_z_inches)
    {
        for (int local_x_inches = 0; local_x_inches < patch_resolution_; ++local_x_inches)
        {
            maximum_height = std::max(
                maximum_height,
                fine_top_height_inches_at(coarse_x, coarse_z, local_x_inches, local_z_inches));
        }
    }

    return maximum_height;
}

bool GravityErosionField::patch_varies_from_coarse_at(int coarse_x, int coarse_z) const
{
    const int coarse_height = coarse_top_height_inches_at(coarse_x, coarse_z);
    return patch_min_height_inches_at(coarse_x, coarse_z) != coarse_height
        || patch_max_height_inches_at(coarse_x, coarse_z) != coarse_height;
}

void GravityErosionField::step_cycle()
{
    std::vector<std::int8_t> deltas(fine_surface_heights_inches_.size(), 0);

    for (int fine_z = 0; fine_z < fine_depth(); ++fine_z)
    {
        for (int fine_x = 0; fine_x < fine_width(); ++fine_x)
        {
            if (erodible_at_global(fine_x, fine_z) == 0)
            {
                continue;
            }

            const int current_height = fine_height_at_global(fine_x, fine_z);
            int lowest_neighbor_height = current_height;
            int lowest_neighbor_x = fine_x;
            int lowest_neighbor_z = fine_z;

            const std::array<std::pair<int, int>, 4> neighbor_offsets{{
                {-1, 0},
                {1, 0},
                {0, -1},
                {0, 1},
            }};

            for (const auto &[offset_x, offset_z] : neighbor_offsets)
            {
                const int neighbor_x = fine_x + offset_x;
                const int neighbor_z = fine_z + offset_z;

                if (neighbor_x < 0 || neighbor_x >= fine_width() || neighbor_z < 0 || neighbor_z >= fine_depth())
                {
                    continue;
                }

                if (erodible_at_global(neighbor_x, neighbor_z) == 0)
                {
                    continue;
                }

                const int neighbor_height = fine_height_at_global(neighbor_x, neighbor_z);
                if (neighbor_height < lowest_neighbor_height)
                {
                    lowest_neighbor_height = neighbor_height;
                    lowest_neighbor_x = neighbor_x;
                    lowest_neighbor_z = neighbor_z;
                }
            }

            if (current_height - lowest_neighbor_height < 2)
            {
                continue;
            }

            deltas[fine_index_of(fine_x, fine_z)] -= 1;
            deltas[fine_index_of(lowest_neighbor_x, lowest_neighbor_z)] += 1;
        }
    }

    for (std::size_t index = 0; index < fine_surface_heights_inches_.size(); ++index)
    {
        fine_surface_heights_inches_[index] = static_cast<std::int16_t>(
            fine_surface_heights_inches_[index] + deltas[index]);
    }

    ++cycle_count_;
}

std::size_t GravityErosionField::coarse_index_of(int coarse_x, int coarse_z) const
{
    if (coarse_x < 0 || coarse_x >= coarse_width_ || coarse_z < 0 || coarse_z >= coarse_depth_)
    {
        throw std::out_of_range("GravityErosionField coarse coordinates are out of range.");
    }

    return static_cast<std::size_t>(coarse_z * coarse_width_ + coarse_x);
}

std::size_t GravityErosionField::fine_index_of(int fine_x, int fine_z) const
{
    if (fine_x < 0 || fine_x >= fine_width() || fine_z < 0 || fine_z >= fine_depth())
    {
        throw std::out_of_range("GravityErosionField fine coordinates are out of range.");
    }

    return static_cast<std::size_t>(fine_z * fine_width() + fine_x);
}

int GravityErosionField::fine_width() const noexcept
{
    return coarse_width_ * patch_resolution_;
}

int GravityErosionField::fine_depth() const noexcept
{
    return coarse_depth_ * patch_resolution_;
}

bool GravityErosionField::is_erodible_material(TerrainMaterial material) const noexcept
{
    switch (material)
    {
    case TerrainMaterial::Grass:
    case TerrainMaterial::GardenLoam:
    case TerrainMaterial::WetSoil:
        return true;
    case TerrainMaterial::PackedEarth:
    case TerrainMaterial::PathStone:
    case TerrainMaterial::AncientBrick:
    case TerrainMaterial::PoolWater:
        return false;
    }

    return false;
}

int GravityErosionField::fine_height_at_global(int fine_x, int fine_z) const
{
    return fine_surface_heights_inches_.at(fine_index_of(fine_x, fine_z));
}

std::int16_t &GravityErosionField::fine_height_at_global(int fine_x, int fine_z)
{
    return fine_surface_heights_inches_.at(fine_index_of(fine_x, fine_z));
}

std::uint8_t GravityErosionField::erodible_at_global(int fine_x, int fine_z) const
{
    return erodible_mask_.at(fine_index_of(fine_x, fine_z));
}
} // namespace grannys_house_trials::sim
