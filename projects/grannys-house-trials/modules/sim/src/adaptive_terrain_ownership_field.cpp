#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"

#include <algorithm>
#include <stdexcept>

namespace grannys_house_trials::sim
{
namespace
{
constexpr int inches_per_foot = GravityErosionField::inches_per_foot();

[[nodiscard]] int ceil_divide_positive(int numerator, int denominator) noexcept
{
    if (numerator <= 0)
    {
        return 0;
    }

    return (numerator + denominator - 1) / denominator;
}
} // namespace

AdaptiveTerrainOwnershipField::AdaptiveTerrainOwnershipField(
    const GrassField &coarse_field,
    const GravityErosionField &erosion_field)
{
    rebuild(coarse_field, erosion_field);
}

void AdaptiveTerrainOwnershipField::rebuild(
    const GrassField &coarse_field,
    const GravityErosionField &erosion_field)
{
    coarse_width_ = coarse_field.width();
    coarse_depth_ = coarse_field.depth();
    max_non_empty_block_count_ = 0;
    ownership_.assign(static_cast<std::size_t>(coarse_width_ * coarse_depth_), OwnershipSummary{});

    for (int coarse_z = 0; coarse_z < coarse_depth_; ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < coarse_width_; ++coarse_x)
        {
            const int patch_min_height_inches = erosion_field.patch_min_height_inches_at(coarse_x, coarse_z);
            const int patch_max_height_inches = erosion_field.patch_max_height_inches_at(coarse_x, coarse_z);

            OwnershipSummary summary{};
            summary.full_block_count = static_cast<std::int16_t>(
                std::max(0, patch_min_height_inches / inches_per_foot));

            const int highest_non_empty_block_count =
                ceil_divide_positive(patch_max_height_inches, inches_per_foot);
            summary.refined_block_count = static_cast<std::int16_t>(
                std::max(0, highest_non_empty_block_count - summary.full_block_count));

            ownership_[index_of(coarse_x, coarse_z)] = summary;
            max_non_empty_block_count_ = std::max(max_non_empty_block_count_, highest_non_empty_block_count);
        }
    }
}

int AdaptiveTerrainOwnershipField::coarse_width() const noexcept
{
    return coarse_width_;
}

int AdaptiveTerrainOwnershipField::coarse_depth() const noexcept
{
    return coarse_depth_;
}

int AdaptiveTerrainOwnershipField::max_non_empty_block_count() const noexcept
{
    return max_non_empty_block_count_;
}

int AdaptiveTerrainOwnershipField::full_block_count_at(int coarse_x, int coarse_z) const
{
    return ownership_.at(index_of(coarse_x, coarse_z)).full_block_count;
}

int AdaptiveTerrainOwnershipField::refined_block_count_at(int coarse_x, int coarse_z) const
{
    return ownership_.at(index_of(coarse_x, coarse_z)).refined_block_count;
}

int AdaptiveTerrainOwnershipField::highest_non_empty_block_count_at(int coarse_x, int coarse_z) const
{
    const OwnershipSummary &summary = ownership_.at(index_of(coarse_x, coarse_z));
    return summary.full_block_count + summary.refined_block_count;
}

TerrainVolumeOwnership AdaptiveTerrainOwnershipField::ownership_at(int coarse_x, int coarse_z, int block_y) const
{
    if (block_y < 0)
    {
        throw std::out_of_range("AdaptiveTerrainOwnershipField block_y is out of range.");
    }

    const OwnershipSummary &summary = ownership_.at(index_of(coarse_x, coarse_z));
    if (block_y < summary.full_block_count)
    {
        return TerrainVolumeOwnership::coarse_full_block;
    }

    if (block_y < summary.full_block_count + summary.refined_block_count)
    {
        return TerrainVolumeOwnership::refined_inch_volume;
    }

    return TerrainVolumeOwnership::empty;
}

std::size_t AdaptiveTerrainOwnershipField::index_of(int coarse_x, int coarse_z) const
{
    if (coarse_x < 0 || coarse_x >= coarse_width_ || coarse_z < 0 || coarse_z >= coarse_depth_)
    {
        throw std::out_of_range("AdaptiveTerrainOwnershipField coarse coordinates are out of range.");
    }

    return static_cast<std::size_t>(coarse_z * coarse_width_ + coarse_x);
}
} // namespace grannys_house_trials::sim
