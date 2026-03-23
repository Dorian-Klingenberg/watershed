#pragma once

#include <vector>

namespace voxel_infrastructure_004
{
enum class MaterialKind
{
    Air,
    SurfaceWater,
    MarshSoil,
    TerraceFill,
    Bedrock,
    AncientConduit,
    DelayBasin,
    Collector,
    InspectionShaft
};

struct WorldVoxel
{
    MaterialKind material = MaterialKind::Air;
    bool hidden = false;
};

struct WorldDefinition
{
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<WorldVoxel> voxels;
};

[[nodiscard]] WorldDefinition build_world_definition();
} // namespace voxel_infrastructure_004
