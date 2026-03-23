#pragma once

#include "simulation.h"

namespace scalar_field_flooding
{
[[nodiscard]] TerrainProperties terrain_properties(TerrainType terrain);
[[nodiscard]] const char* terrain_name(TerrainType terrain);
[[nodiscard]] float terrain_airability_potential(TerrainType terrain);
} // namespace scalar_field_flooding
