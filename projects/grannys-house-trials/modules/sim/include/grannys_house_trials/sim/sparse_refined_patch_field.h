#pragma once

#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace grannys_house_trials::sim
{
class SparseRefinedPatchField
{
public:
    static constexpr int invalid_patch_index = -1;
    static constexpr int patch_cell_count = DetailPatch::resolution * DetailPatch::resolution;

    struct RefinedPatch
    {
        std::int16_t coarse_x = 0;
        std::int16_t coarse_z = 0;
        std::int16_t coarse_full_height_inches = 0;
        std::int16_t max_height_inches = 0;
        std::array<std::int16_t, patch_cell_count> top_heights_inches{};
    };

    SparseRefinedPatchField(
        const GrassField &coarse_field,
        const GravityErosionField &erosion_field,
        const AdaptiveTerrainOwnershipField &ownership_field);

    [[nodiscard]] int coarse_width() const noexcept;
    [[nodiscard]] int coarse_depth() const noexcept;
    [[nodiscard]] int patch_resolution() const noexcept;
    [[nodiscard]] int patch_count() const noexcept;

    [[nodiscard]] bool has_patch_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] int patch_index_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] const RefinedPatch &patch_at_index(int patch_index) const;

    void rebuild(
        const GrassField &coarse_field,
        const GravityErosionField &erosion_field,
        const AdaptiveTerrainOwnershipField &ownership_field);

private:
    [[nodiscard]] std::size_t coarse_index_of(int coarse_x, int coarse_z) const;

    int coarse_width_ = 0;
    int coarse_depth_ = 0;
    int patch_resolution_ = DetailPatch::resolution;
    std::vector<int> patch_lookup_;
    std::vector<RefinedPatch> patches_;
};
} // namespace grannys_house_trials::sim
