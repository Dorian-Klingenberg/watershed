#include "world_definition.h"

namespace voxel_infrastructure_004
{
namespace
{
int index_of(int width, int height, int x, int y, int z)
{
    return (z * height + y) * width + x;
}
} // namespace

WorldDefinition build_world_definition()
{
    constexpr int width = 14;
    constexpr int height = 8;
    constexpr int depth = 5;

    WorldDefinition world;
    world.width = width;
    world.height = height;
    world.depth = depth;
    world.voxels.assign(static_cast<std::size_t>(width * height * depth), {});

    for (int z = 0; z < depth; ++z)
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                WorldVoxel &voxel = world.voxels[static_cast<std::size_t>(index_of(width, height, x, y, z))];

                if (y == height - 1)
                {
                    voxel.material = (z >= 1 && z <= 3 && x >= 2 && x <= 8) ? MaterialKind::SurfaceWater : MaterialKind::Air;
                    continue;
                }

                if (y == height - 2)
                {
                    voxel.material = (z >= 1 && z <= 3 && x >= 2 && x <= 8) ? MaterialKind::MarshSoil : MaterialKind::TerraceFill;
                    voxel.hidden = false;
                    continue;
                }

                if (y >= 3)
                {
                    voxel.material = MaterialKind::TerraceFill;
                    voxel.hidden = false;
                    continue;
                }

                voxel.material = MaterialKind::Bedrock;
                voxel.hidden = true;
            }
        }
    }

    for (int x = 1; x <= 10; ++x)
    {
        WorldVoxel &voxel = world.voxels[static_cast<std::size_t>(index_of(width, height, x, 2, 2))];
        voxel.material = MaterialKind::AncientConduit;
        voxel.hidden = true;
    }

    for (int z = 1; z <= 3; ++z)
    {
        WorldVoxel &voxel = world.voxels[static_cast<std::size_t>(index_of(width, height, 8, 2, z))];
        voxel.material = MaterialKind::DelayBasin;
        voxel.hidden = true;
    }

    for (int x = 10; x <= 12; ++x)
    {
        WorldVoxel &voxel = world.voxels[static_cast<std::size_t>(index_of(width, height, x, 3, 2))];
        voxel.material = MaterialKind::Collector;
        voxel.hidden = true;
    }

    return world;
}
} // namespace voxel_infrastructure_004
