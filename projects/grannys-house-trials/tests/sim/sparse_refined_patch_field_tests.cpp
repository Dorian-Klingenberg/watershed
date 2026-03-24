#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"
#include "grannys_house_trials/sim/sparse_refined_patch_field.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::sim::AdaptiveTerrainOwnershipField;
using grannys_house_trials::sim::GrassField;
using grannys_house_trials::sim::GravityErosionField;
using grannys_house_trials::sim::SparseRefinedPatchField;

TEST_CASE("SparseRefinedPatchField excludes untouched full columns", "[sim][sparse_refined_patch]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);
    const SparseRefinedPatchField refined_field(coarse_field, erosion_field, ownership_field);

    REQUIRE_FALSE(refined_field.has_patch_at(20, 20));
    REQUIRE(refined_field.patch_index_at(20, 20) == SparseRefinedPatchField::invalid_patch_index);
    REQUIRE(refined_field.patch_count() < coarse_field.width() * coarse_field.depth());
}

TEST_CASE("SparseRefinedPatchField stores only promoted inch patches", "[sim][sparse_refined_patch]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);
    const SparseRefinedPatchField refined_field(coarse_field, erosion_field, ownership_field);

    REQUIRE(refined_field.has_patch_at(84, 51));

    const auto &patch = refined_field.patch_at_index(refined_field.patch_index_at(84, 51));
    REQUIRE(patch.coarse_x == 84);
    REQUIRE(patch.coarse_z == 51);
    REQUIRE(patch.coarse_full_height_inches == ownership_field.full_block_count_at(84, 51) * refined_field.patch_resolution());
    REQUIRE(patch.max_height_inches == erosion_field.patch_max_height_inches_at(84, 51));
}

TEST_CASE("SparseRefinedPatchField lookup matches adaptive ownership refinement", "[sim][sparse_refined_patch]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);
    const AdaptiveTerrainOwnershipField ownership_field(coarse_field, erosion_field);
    const SparseRefinedPatchField refined_field(coarse_field, erosion_field, ownership_field);

    for (int coarse_z = 0; coarse_z < coarse_field.depth(); ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < coarse_field.width(); ++coarse_x)
        {
            REQUIRE(refined_field.has_patch_at(coarse_x, coarse_z) == (ownership_field.refined_block_count_at(coarse_x, coarse_z) > 0));
        }
    }
}
