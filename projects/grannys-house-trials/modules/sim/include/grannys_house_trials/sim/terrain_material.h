#pragma once

#include <array>
#include <string_view>

namespace grannys_house_trials::sim
{
enum class TerrainMaterial
{
    Grass,
    GardenLoam,
    PackedEarth,
    PathStone,
    WetSoil,
    AncientBrick,
    PoolWater,
};

inline constexpr std::array<TerrainMaterial, 7> all_terrain_materials{
    TerrainMaterial::Grass,
    TerrainMaterial::GardenLoam,
    TerrainMaterial::PackedEarth,
    TerrainMaterial::PathStone,
    TerrainMaterial::WetSoil,
    TerrainMaterial::AncientBrick,
    TerrainMaterial::PoolWater,
};

[[nodiscard]] constexpr std::string_view terrain_material_name(TerrainMaterial material) noexcept
{
    switch (material)
    {
    case TerrainMaterial::Grass:
        return "Grass";
    case TerrainMaterial::GardenLoam:
        return "Garden Loam";
    case TerrainMaterial::PackedEarth:
        return "Packed Earth";
    case TerrainMaterial::PathStone:
        return "Path Stone";
    case TerrainMaterial::WetSoil:
        return "Wet Soil";
    case TerrainMaterial::AncientBrick:
        return "Ancient Brick";
    case TerrainMaterial::PoolWater:
        return "Pool Water";
    }

    return "Unknown";
}
} // namespace grannys_house_trials::sim
