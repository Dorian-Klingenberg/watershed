#pragma once

#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grannys_house_trials::sim
{
enum class TerrainVolumeOwnership
{
    empty,
    coarse_full_block,
    refined_inch_volume,
};

class AdaptiveTerrainOwnershipField
{
public:
    AdaptiveTerrainOwnershipField(
        const GrassField &coarse_field,
        const GravityErosionField &erosion_field);

    void rebuild(
        const GrassField &coarse_field,
        const GravityErosionField &erosion_field);

    [[nodiscard]] int coarse_width() const noexcept;
    [[nodiscard]] int coarse_depth() const noexcept;
    [[nodiscard]] int max_non_empty_block_count() const noexcept;
    [[nodiscard]] int full_block_count_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] int refined_block_count_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] int highest_non_empty_block_count_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] TerrainVolumeOwnership ownership_at(int coarse_x, int coarse_z, int block_y) const;

private:
    struct OwnershipSummary
    {
        std::int16_t full_block_count = 0;
        std::int16_t refined_block_count = 0;
    };

    [[nodiscard]] std::size_t index_of(int coarse_x, int coarse_z) const;

    int coarse_width_ = 0;
    int coarse_depth_ = 0;
    int max_non_empty_block_count_ = 0;
    std::vector<OwnershipSummary> ownership_;
};
} // namespace grannys_house_trials::sim
