#pragma once

namespace grannys_house_trials::gfx
{
class OrbitCamera
{
public:
    OrbitCamera();
    OrbitCamera(float yaw_degrees, float pitch_degrees, float distance);

    void orbit(float yaw_delta_degrees, float pitch_delta_degrees);
    void move_focus(float delta_x, float delta_y, float delta_z);
    void zoom(float distance_delta);

    [[nodiscard]] float yaw_degrees() const noexcept;
    [[nodiscard]] float pitch_degrees() const noexcept;
    [[nodiscard]] float distance() const noexcept;
    [[nodiscard]] float focus_x() const noexcept;
    [[nodiscard]] float focus_y() const noexcept;
    [[nodiscard]] float focus_z() const noexcept;

private:
    static constexpr float minimum_pitch_degrees = -85.0f;
    static constexpr float maximum_pitch_degrees = 85.0f;
    static constexpr float minimum_distance = 1.0f;

    float yaw_degrees_ = 45.0f;
    float pitch_degrees_ = 25.0f;
    float distance_ = 10.0f;
    float focus_x_ = 0.0f;
    float focus_y_ = 0.5f;
    float focus_z_ = 0.0f;
};
} // namespace grannys_house_trials::gfx
