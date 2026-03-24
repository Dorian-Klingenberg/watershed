#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::sim::GrassField;
using grannys_house_trials::sim::GravityErosionField;

namespace
{
long long total_fine_height_inches(const GravityErosionField &erosion_field)
{
    long long total_height = 0;

    for (int coarse_z = 0; coarse_z < erosion_field.coarse_depth(); ++coarse_z)
    {
        for (int coarse_x = 0; coarse_x < erosion_field.coarse_width(); ++coarse_x)
        {
            for (int local_z_inches = 0; local_z_inches < erosion_field.patch_resolution(); ++local_z_inches)
            {
                for (int local_x_inches = 0; local_x_inches < erosion_field.patch_resolution(); ++local_x_inches)
                {
                    total_height += erosion_field.fine_top_height_inches_at(
                        coarse_x,
                        coarse_z,
                        local_x_inches,
                        local_z_inches);
                }
            }
        }
    }

    return total_height;
}
} // namespace

TEST_CASE("GravityErosionField mirrors the coarse field at initialization", "[sim][gravity_erosion]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    const GravityErosionField erosion_field(coarse_field);

    REQUIRE(erosion_field.coarse_width() == coarse_field.width());
    REQUIRE(erosion_field.coarse_depth() == coarse_field.depth());
    REQUIRE(erosion_field.patch_resolution() == GrassField::detail_patch_resolution());
    REQUIRE(erosion_field.cycle_count() == 0);

    REQUIRE(erosion_field.coarse_top_height_inches_at(20, 20) == coarse_field.coarse_top_height_inches_at(20, 20));
    REQUIRE(erosion_field.coarse_top_height_inches_at(84, 51) == coarse_field.coarse_top_height_inches_at(84, 51));
    REQUIRE(erosion_field.fine_top_height_inches_at(84, 51, 6, 0) == coarse_field.fine_top_height_inches_at(84, 51, 6, 0));
    REQUIRE(erosion_field.fine_top_height_inches_at(90, 39, 6, 6) == coarse_field.fine_top_height_inches_at(90, 39, 6, 6));
}

TEST_CASE("GravityErosionField smooths erodible garden furrows in one-inch steps", "[sim][gravity_erosion]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    GravityErosionField erosion_field(coarse_field);

    const int before_ridge_height = erosion_field.fine_top_height_inches_at(84, 51, 6, 3);
    const int before_trough_height = erosion_field.fine_top_height_inches_at(84, 51, 6, 4);
    REQUIRE(before_ridge_height - before_trough_height >= 2);

    erosion_field.step_cycle();

    const int after_ridge_height = erosion_field.fine_top_height_inches_at(84, 51, 6, 3);
    const int after_trough_height = erosion_field.fine_top_height_inches_at(84, 51, 6, 4);

    REQUIRE(erosion_field.cycle_count() == 1);
    REQUIRE(after_ridge_height == before_ridge_height - 1);
    REQUIRE(after_trough_height > before_trough_height);
}

TEST_CASE("GravityErosionField conserves total fine surface height across a cycle", "[sim][gravity_erosion]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    GravityErosionField erosion_field(coarse_field);

    const long long total_before = total_fine_height_inches(erosion_field);

    erosion_field.step_cycle();

    const long long total_after = total_fine_height_inches(erosion_field);
    REQUIRE(total_after == total_before);
}

TEST_CASE("GravityErosionField leaves non-erodible pool and brick cells unchanged", "[sim][gravity_erosion]")
{
    const GrassField coarse_field(100, 100, 1.0f);
    GravityErosionField erosion_field(coarse_field);

    const int pool_before = erosion_field.fine_top_height_inches_at(90, 39, 6, 6);
    const int rim_before = erosion_field.fine_top_height_inches_at(87, 39, 0, 6);

    erosion_field.step_cycle();
    erosion_field.step_cycle();
    erosion_field.step_cycle();

    REQUIRE(erosion_field.fine_top_height_inches_at(90, 39, 6, 6) == pool_before);
    REQUIRE(erosion_field.fine_top_height_inches_at(87, 39, 0, 6) == rim_before);
}
