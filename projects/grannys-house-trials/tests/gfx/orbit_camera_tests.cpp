#include "grannys_house_trials/gfx/orbit_camera.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::gfx::OrbitCamera;

TEST_CASE("OrbitCamera clamps pitch when orbiting", "[gfx][orbit_camera]")
{
    OrbitCamera camera;
    camera.orbit(0.0f, 500.0f);

    REQUIRE(camera.pitch_degrees() == 85.0f);
}

TEST_CASE("OrbitCamera clamps distance when zooming", "[gfx][orbit_camera]")
{
    OrbitCamera camera;
    camera.zoom(-1000.0f);

    REQUIRE(camera.distance() == 1.0f);
}

TEST_CASE("OrbitCamera tracks focus movement", "[gfx][orbit_camera]")
{
    OrbitCamera camera;
    camera.move_focus(3.0f, 1.5f, -2.0f);

    REQUIRE(camera.focus_x() == 3.0f);
    REQUIRE(camera.focus_y() == 2.0f);
    REQUIRE(camera.focus_z() == -2.0f);
}
