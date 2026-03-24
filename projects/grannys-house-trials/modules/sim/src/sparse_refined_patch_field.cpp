#include "grannys_house_trials/sim/sparse_refined_patch_field.h"

#include <stdexcept>

namespace grannys_house_trials::sim
{
SparseRefinedPatchField::SparseRefinedPatchField(
    const GrassField &coarse_field,
    const GravityErosionField &erosion_field,
    const AdaptiveTerrainOwnershipField &ownership_field)
{
    rebuild(coarse_field, erosion_field, ownership_field);
}

int SparseRefinedPatchField::coarse_width() const noexcept
{
    return coarse_width_;
}

int SparseRefinedPatchField::coarse_depth() const noexcept
{
    return coarse_depth_;
}

int SparseRefinedPatchField::patch_resolution() const noexcept
{
    return patch_resolution_;
}

int SparseRefinedPatchField::patch_count() const noexcept
{
    return static_cast<int>(patches_.size());
}

bool SparseRefinedPatchField::has_patch_at(int coarse_x, int coarse_z) const
{
    return patch_index_at(coarse_x, coarse_z) != invalid_patch_index;
}

int SparseRefinedPatchField::patch_index_at(int coarse_x, int coarse_z) const
{
    return patch_lookup_.at(coarse_index_of(coarse_x, coarse_z));
}

const SparseRefinedPatchField::RefinedPatch &SparseRefinedPatchField::patch_at_index(int patch_index) const
{
    return patches_.at(static_cast<std::size_t>(patch_index));
}

void SparseRefinedPatchField::rebuild(
    const GrassField &coarse_field,
    const GravityErosionField &erosion_field,
    const AdaptiveTerrainOwnershipField &ownership_field)
{
    coarse_width_ = coarse_field.width();
    coarse_depth_ = coarse_field.depth();
    patch_lookup_.assign(static_cast<std::size_t>(coarse_width_ * coarse_depth_), invalid_patch_index);
    patches_.clear();

    for (int coarse_z = 0; coarse_z < coarse_depth_; ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < coarse_width_; ++coarse_x)
        {
            if (ownership_field.refined_block_count_at(coarse_x, coarse_z) <= 0)
            {
                continue;
            }

            RefinedPatch patch{};
            patch.coarse_x = static_cast<std::int16_t>(coarse_x);
            patch.coarse_z = static_cast<std::int16_t>(coarse_z);
            patch.coarse_full_height_inches = static_cast<std::int16_t>(
                ownership_field.full_block_count_at(coarse_x, coarse_z) * patch_resolution_);

            int cell_index = 0;
            for (int local_z_inches = 0; local_z_inches < patch_resolution_; ++local_z_inches)
            {
                for (int local_x_inches = 0; local_x_inches < patch_resolution_; ++local_x_inches)
                {
                    const int top_height_inches = erosion_field.fine_top_height_inches_at(
                        coarse_x,
                        coarse_z,
                        local_x_inches,
                        local_z_inches);
                    patch.top_heights_inches[static_cast<std::size_t>(cell_index)] =
                        static_cast<std::int16_t>(top_height_inches);
                    patch.max_height_inches = static_cast<std::int16_t>(
                        std::max(static_cast<int>(patch.max_height_inches), top_height_inches));
                    ++cell_index;
                }
            }

            patch_lookup_[coarse_index_of(coarse_x, coarse_z)] = static_cast<int>(patches_.size());
            patches_.push_back(patch);
        }
    }
}

std::size_t SparseRefinedPatchField::coarse_index_of(int coarse_x, int coarse_z) const
{
    if (coarse_x < 0 || coarse_x >= coarse_width_ || coarse_z < 0 || coarse_z >= coarse_depth_)
    {
        throw std::out_of_range("SparseRefinedPatchField coarse coordinates are out of range.");
    }

    return static_cast<std::size_t>(coarse_z * coarse_width_ + coarse_x);
}
} // namespace grannys_house_trials::sim
