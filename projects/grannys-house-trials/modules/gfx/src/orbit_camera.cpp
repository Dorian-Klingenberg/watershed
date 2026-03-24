#include "grannys_house_trials/gfx/orbit_camera.h"

#include <algorithm>

namespace grannys_house_trials::gfx
{
OrbitCamera::OrbitCamera() = default;

OrbitCamera::OrbitCamera(float yaw_degrees, float pitch_degrees, float distance)
    : yaw_degrees_(yaw_degrees)
    , pitch_degrees_(std::clamp(pitch_degrees, minimum_pitch_degrees, maximum_pitch_degrees))
    , distance_(std::max(distance, minimum_distance))
{
}

void OrbitCamera::orbit(float yaw_delta_degrees, float pitch_delta_degrees)
{
    yaw_degrees_ += yaw_delta_degrees;
    pitch_degrees_ = std::clamp(
        pitch_degrees_ + pitch_delta_degrees,
        minimum_pitch_degrees,
        maximum_pitch_degrees);
}

void OrbitCamera::move_focus(float delta_x, float delta_y, float delta_z)
{
    focus_x_ += delta_x;
    focus_y_ += delta_y;
    focus_z_ += delta_z;
}

void OrbitCamera::zoom(float distance_delta)
{
    distance_ = std::max(distance_ + distance_delta, minimum_distance);
}

float OrbitCamera::yaw_degrees() const noexcept
{
    return yaw_degrees_;
}

float OrbitCamera::pitch_degrees() const noexcept
{
    return pitch_degrees_;
}

float OrbitCamera::distance() const noexcept
{
    return distance_;
}

float OrbitCamera::focus_x() const noexcept
{
    return focus_x_;
}

float OrbitCamera::focus_y() const noexcept
{
    return focus_y_;
}

float OrbitCamera::focus_z() const noexcept
{
    return focus_z_;
}
} // namespace grannys_house_trials::gfx
