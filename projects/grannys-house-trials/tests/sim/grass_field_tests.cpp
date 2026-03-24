#include "grannys_house_trials/sim/grass_field.h"
#include "grannys_house_trials/sim/terrain_material.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using grannys_house_trials::sim::GrassField;
using grannys_house_trials::sim::TerrainMaterial;
using grannys_house_trials::sim::all_terrain_materials;

TEST_CASE("GrassField stores its configured size", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.width() == 100);
    REQUIRE(field.depth() == 100);
    REQUIRE(field.voxel_size_feet() == 1.0f);
    REQUIRE(field.cell_count() == static_cast<std::size_t>(100 * 100));
}

TEST_CASE("GrassField defaults every cell to grass", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(0, 0).material == TerrainMaterial::Grass);
    REQUIRE(field.at(50, 50).material == TerrainMaterial::Grass);
    REQUIRE(field.at(99, 99).material == TerrainMaterial::Grass);
}

TEST_CASE("GrassField rejects out-of-range coordinates", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE_THROWS_AS(field.at(-1, 0), std::out_of_range);
    REQUIRE_THROWS_AS(field.at(100, 0), std::out_of_range);
    REQUIRE_THROWS_AS(field.at(0, 100), std::out_of_range);
}

TEST_CASE("GrassField uses perlin-style height variation between one and five voxels", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    int minimum_height = field.at(0, 0).column_height_voxels;
    int maximum_height = minimum_height;
    std::array<bool, 5> seen_heights{};

    for (int z = 0; z < field.depth(); ++z)
    {
        for (int x = 0; x < field.width(); ++x)
        {
            const int height = field.at(x, z).column_height_voxels;
            minimum_height = std::min(minimum_height, height);
            maximum_height = std::max(maximum_height, height);
            REQUIRE(height >= 1);
            REQUIRE(height <= 5);
            seen_heights[static_cast<std::size_t>(height - 1)] = true;
        }
    }

    REQUIRE(minimum_height == 1);
    REQUIRE(maximum_height == 5);
    REQUIRE(seen_heights[0]);
    REQUIRE(seen_heights[1]);
    REQUIRE(seen_heights[2]);
    REQUIRE(seen_heights[3]);
    REQUIRE(seen_heights[4]);
}

TEST_CASE("GrassField includes a flattened homestead pad", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(64, 50).is_homestead_pad);
    REQUIRE(field.at(64, 50).column_height_voxels == 2);
    REQUIRE_FALSE(field.at(20, 20).is_homestead_pad);
}

TEST_CASE("GrassField marks a small garden bed with richer attributes", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    const auto &garden_bed = field.at(84, 51);
    const auto &far_grass = field.at(20, 20);

    REQUIRE(garden_bed.garden.is_garden_bed);
    REQUIRE(garden_bed.garden.fertility > far_grass.garden.fertility);
    REQUIRE(garden_bed.garden.soil_moisture > far_grass.garden.soil_moisture);
}

TEST_CASE("GrassField shapes the garden into terraces", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(82, 39).material == TerrainMaterial::GardenLoam);
    REQUIRE(field.at(82, 39).column_height_voxels == 4);
    REQUIRE(field.at(82, 45).material == TerrainMaterial::GardenLoam);
    REQUIRE(field.at(82, 45).column_height_voxels == 3);
    REQUIRE(field.at(82, 51).material == TerrainMaterial::GardenLoam);
    REQUIRE(field.at(82, 51).column_height_voxels == 2);
}

TEST_CASE("GrassField includes a small garden pool", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(90, 39).material == TerrainMaterial::PoolWater);
    REQUIRE(field.at(90, 39).column_height_voxels == 1);
    REQUIRE(field.at(90, 39).garden.soil_moisture == 1.0f);
}

TEST_CASE("GrassField gives the pool and terraces visible retaining edges", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(87, 39).material == TerrainMaterial::AncientBrick);
    REQUIRE(field.at(87, 39).column_height_voxels == 4);
    REQUIRE(field.at(76, 40).material == TerrainMaterial::AncientBrick);
    REQUIRE(field.at(76, 40).column_height_voxels == 4);
    REQUIRE(field.at(78, 46).material == TerrainMaterial::AncientBrick);
    REQUIRE(field.at(78, 46).column_height_voxels == 3);
    REQUIRE(field.at(80, 53).material == TerrainMaterial::AncientBrick);
    REQUIRE(field.at(80, 53).column_height_voxels == 2);
}

TEST_CASE("GrassField exposes a sparse inch-scale detail patch layer for fine edits", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.has_detail_patch(90, 39));
    REQUIRE(field.has_detail_patch(84, 51));
    REQUIRE(field.has_detail_patch(87, 39));
    REQUIRE_FALSE(field.has_detail_patch(20, 20));
}

TEST_CASE("GrassField detail patches can add and subtract inches from the coarse surface", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    const int pool_coarse_top = field.coarse_top_height_inches_at(90, 39);
    const int pool_fine_top = field.fine_top_height_inches_at(90, 39, 6, 6);
    REQUIRE(pool_fine_top < pool_coarse_top);

    const int terrace_coarse_top = field.coarse_top_height_inches_at(84, 51);
    const int terrace_furrow_top = field.fine_top_height_inches_at(84, 51, 6, 0);
    const int terrace_ridge_top = field.fine_top_height_inches_at(84, 51, 6, 3);
    REQUIRE(terrace_furrow_top < terrace_coarse_top);
    REQUIRE(terrace_ridge_top > terrace_furrow_top);

    const int rim_coarse_top = field.coarse_top_height_inches_at(87, 39);
    const int rim_edge_top = field.fine_top_height_inches_at(87, 39, 0, 6);
    REQUIRE(rim_edge_top > rim_coarse_top);
}

TEST_CASE("GrassField scatters at least one cell of every defined terrain material", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    std::array<bool, all_terrain_materials.size()> found_materials{};

    for (int z = 0; z < field.depth(); ++z)
    {
        for (int x = 0; x < field.width(); ++x)
        {
            const auto material = field.at(x, z).material;

            for (std::size_t index = 0; index < all_terrain_materials.size(); ++index)
            {
                if (all_terrain_materials[index] == material)
                {
                    found_materials[index] = true;
                }
            }
        }
    }

    for (bool found_material : found_materials)
    {
        REQUIRE(found_material);
    }
}

TEST_CASE("GrassField uses representative Granny's Yard materials in stable sample patches", "[sim][grass_field]")
{
    const GrassField field(100, 100, 1.0f);

    REQUIRE(field.at(20, 20).material == TerrainMaterial::Grass);
    REQUIRE(field.at(84, 51).material == TerrainMaterial::GardenLoam);
    REQUIRE(field.at(64, 50).material == TerrainMaterial::PackedEarth);
    REQUIRE(field.at(70, 61).material == TerrainMaterial::PathStone);
    REQUIRE(field.at(22, 76).material == TerrainMaterial::WetSoil);
    REQUIRE(field.at(32, 20).material == TerrainMaterial::AncientBrick);
    REQUIRE(field.at(90, 39).material == TerrainMaterial::PoolWater);
    REQUIRE(field.at(87, 39).material == TerrainMaterial::AncientBrick);
}
