#pragma once

#include "grannys_house_trials/sim/terrain_material.h"

#include <cstddef>
#include <array>
#include <cstdint>
#include <vector>

namespace grannys_house_trials::sim
{
struct GardenAttributes
{
    float soil_moisture = 0.0f;
    float fertility = 0.0f;
    float sunlight = 0.0f;
    float weed_pressure = 0.0f;
    bool is_garden_bed = false;
};

struct GrassVoxel
{
    TerrainMaterial material = TerrainMaterial::Grass;
    int column_height_voxels = 1;
    bool is_homestead_pad = false;
    bool has_detail_patch = false;
    GardenAttributes garden{};
};

struct DetailPatch
{
    static constexpr int resolution = 12;
    static constexpr int cell_count = resolution * resolution;

    bool is_active = false;
    std::array<std::int8_t, cell_count> top_offset_inches{};

    [[nodiscard]] int offset_at(int local_x_inches, int local_z_inches) const;
};

class GrassField
{
public:
    GrassField(int width, int depth, float voxel_size_feet);

    [[nodiscard]] static constexpr int detail_patch_resolution() noexcept
    {
        return DetailPatch::resolution;
    }

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int depth() const noexcept;
    [[nodiscard]] float voxel_size_feet() const noexcept;
    [[nodiscard]] const GrassVoxel &at(int x, int z) const;
    [[nodiscard]] bool has_detail_patch(int x, int z) const;
    [[nodiscard]] const DetailPatch &detail_patch_at(int x, int z) const;
    [[nodiscard]] int coarse_top_height_inches_at(int x, int z) const;
    [[nodiscard]] int fine_top_height_inches_at(
        int x,
        int z,
        int local_x_inches,
        int local_z_inches) const;
    [[nodiscard]] std::size_t cell_count() const noexcept;

private:
    [[nodiscard]] std::size_t index_of(int x, int z) const;

    int width_ = 0;
    int depth_ = 0;
    float voxel_size_feet_ = 1.0f;
    std::vector<GrassVoxel> cells_;
    std::vector<DetailPatch> detail_patches_;
};
} // namespace grannys_house_trials::sim
