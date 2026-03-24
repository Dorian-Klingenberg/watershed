#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::sim::AdaptiveTerrainOwnershipField;
using grannys_house_trials::sim::GrassField;
using grannys_house_trials::sim::GravityErosionField;
using grannys_house_trials::sim::TerrainVolumeOwnership;

TEST_CASE("AdaptiveTerrainOwnershipField keeps uniform untouched columns coarse", "[sim][adaptive_ownership]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);

    const int expected_full_blocks = coarse_field.at(20, 20).column_height_voxels;
    REQUIRE(ownership_field.full_block_count_at(20, 20) == expected_full_blocks);
    REQUIRE(ownership_field.refined_block_count_at(20, 20) == 0);
    REQUIRE(ownership_field.ownership_at(20, 20, expected_full_blocks - 1) == TerrainVolumeOwnership::coarse_full_block);
    REQUIRE(ownership_field.ownership_at(20, 20, expected_full_blocks) == TerrainVolumeOwnership::empty);
}

TEST_CASE("AdaptiveTerrainOwnershipField marks partial garden terraces for inch refinement", "[sim][adaptive_ownership]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);

    REQUIRE(ownership_field.full_block_count_at(84, 51) == 1);
    REQUIRE(ownership_field.refined_block_count_at(84, 51) == 2);
    REQUIRE(ownership_field.ownership_at(84, 51, 0) == TerrainVolumeOwnership::coarse_full_block);
    REQUIRE(ownership_field.ownership_at(84, 51, 1) == TerrainVolumeOwnership::refined_inch_volume);
    REQUIRE(ownership_field.ownership_at(84, 51, 2) == TerrainVolumeOwnership::refined_inch_volume);
    REQUIRE(ownership_field.ownership_at(84, 51, 3) == TerrainVolumeOwnership::empty);
}

TEST_CASE("AdaptiveTerrainOwnershipField treats shallow pool water as a refined top block", "[sim][adaptive_ownership]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);

    REQUIRE(ownership_field.full_block_count_at(90, 39) == 0);
    REQUIRE(ownership_field.refined_block_count_at(90, 39) == 1);
    REQUIRE(ownership_field.ownership_at(90, 39, 0) == TerrainVolumeOwnership::refined_inch_volume);
    REQUIRE(ownership_field.ownership_at(90, 39, 1) == TerrainVolumeOwnership::empty);
}

TEST_CASE("AdaptiveTerrainOwnershipField keeps raised brick rims coarse below and refined only at the cap", "[sim][adaptive_ownership]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);

    REQUIRE(ownership_field.full_block_count_at(87, 39) == 4);
    REQUIRE(ownership_field.refined_block_count_at(87, 39) == 1);
    REQUIRE(ownership_field.ownership_at(87, 39, 3) == TerrainVolumeOwnership::coarse_full_block);
    REQUIRE(ownership_field.ownership_at(87, 39, 4) == TerrainVolumeOwnership::refined_inch_volume);
    REQUIRE(ownership_field.ownership_at(87, 39, 5) == TerrainVolumeOwnership::empty);
}
