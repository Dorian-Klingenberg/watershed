#include "terrain.h"

#include <stdexcept>

namespace scalar_field_flooding
{

// BOOKMARK: terrain_properties
TerrainProperties terrain_properties(TerrainType terrain)
{
    switch (terrain)
    {
    case TerrainType::Bedrock:
        // Bedrock reaches runoff quickly and sheds water once it is full.
        return {.saturation_capacity = 0.18F, .lateral_spread = 0.30F};
    case TerrainType::Clay:
        // Clay collects more than bedrock but trades fast runoff for cling.
        return {.saturation_capacity = 0.48F, .lateral_spread = 0.10F};
    case TerrainType::Loam:
        // Loam can hold substantially more water before it sheds overflow.
        return {.saturation_capacity = 0.72F, .lateral_spread = 0.24F};
    case TerrainType::Sand:
        // Sand admits the most water and moves it through the landscape fastest.
        return {.saturation_capacity = 0.92F, .lateral_spread = 0.44F};
    }

    throw std::invalid_argument("Unknown terrain type.");
}

const char* terrain_name(TerrainType terrain)
{
    switch (terrain)
    {
    case TerrainType::Bedrock:
        return "Bedrock";
    case TerrainType::Clay:
        return "Clay";
    case TerrainType::Loam:
        return "Loam";
    case TerrainType::Sand:
        return "Sand";
    }

    return "Unknown";
}

float terrain_airability_potential(TerrainType terrain)
{
    switch (terrain)
    {
    case TerrainType::Bedrock:
        return 0.10F;
    case TerrainType::Clay:
        return 0.25F;
    case TerrainType::Loam:
        return 0.60F;
    case TerrainType::Sand:
        return 0.90F;
    }

    throw std::invalid_argument("Unknown terrain type.");
}
} // namespace scalar_field_flooding
