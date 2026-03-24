#pragma once

#include "grannys_house_trials/sim/grass_field.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grannys_house_trials::sim
{
class GravityErosionField
{
public:
    explicit GravityErosionField(const GrassField &coarse_field);

    [[nodiscard]] static constexpr int inches_per_foot() noexcept
    {
        return DetailPatch::resolution;
    }

    [[nodiscard]] int coarse_width() const noexcept;
    [[nodiscard]] int coarse_depth() const noexcept;
    [[nodiscard]] int patch_resolution() const noexcept;
    [[nodiscard]] int cycle_count() const noexcept;

    [[nodiscard]] int coarse_top_height_inches_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] int fine_top_height_inches_at(
        int coarse_x,
        int coarse_z,
        int local_x_inches,
        int local_z_inches) const;
    [[nodiscard]] int patch_min_height_inches_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] int patch_max_height_inches_at(int coarse_x, int coarse_z) const;
    [[nodiscard]] bool patch_varies_from_coarse_at(int coarse_x, int coarse_z) const;

    void step_cycle();

private:
    [[nodiscard]] std::size_t coarse_index_of(int coarse_x, int coarse_z) const;
    [[nodiscard]] std::size_t fine_index_of(int fine_x, int fine_z) const;
    [[nodiscard]] int fine_width() const noexcept;
    [[nodiscard]] int fine_depth() const noexcept;
    [[nodiscard]] bool is_erodible_material(TerrainMaterial material) const noexcept;
    [[nodiscard]] int fine_height_at_global(int fine_x, int fine_z) const;
    [[nodiscard]] std::int16_t &fine_height_at_global(int fine_x, int fine_z);
    [[nodiscard]] std::uint8_t erodible_at_global(int fine_x, int fine_z) const;

    int coarse_width_ = 0;
    int coarse_depth_ = 0;
    int patch_resolution_ = DetailPatch::resolution;
    int cycle_count_ = 0;
    std::vector<std::int16_t> coarse_top_heights_inches_;
    std::vector<std::int16_t> fine_surface_heights_inches_;
    std::vector<std::uint8_t> erodible_mask_;
};
} // namespace grannys_house_trials::sim
