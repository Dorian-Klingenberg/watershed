#pragma once

#include "simulation.h"

#include <utility>
#include <vector>

namespace scalar_field_flooding
{
[[nodiscard]] int normalized_coordinate(float fraction, int size);
[[nodiscard]] int terrain_drift_offset(int x, int width, int height);
[[nodiscard]] std::vector<std::pair<int, int>> generated_spring_positions(int width, int height);
[[nodiscard]] GridState make_manual_test_map();
} // namespace scalar_field_flooding
