#pragma once

#include "world_definition.h"

#include <string>
#include <vector>

namespace voxel_infrastructure_004
{
enum class InterventionType
{
    ExcavateInspectionShaft,
    PackTerraceFracture,
    ClearCollectorSpillway,
    VentAncestorWell
};

struct VoxelState
{
    MaterialKind material = MaterialKind::Air;
    double saturation = 0.0;
    double pressure = 0.0;
    double salinity = 0.0;
    double stress = 0.0;
    bool hidden = false;
    bool exposed = false;
};

struct RegionMetrics
{
    double upland_head = 0.0;
    double leak_flux = 0.0;
    double marsh_depth = 0.0;
    double orchard_supply = 0.0;
    double settlement_stability = 0.0;
    double conduit_integrity = 0.0;
    double collector_clearance = 0.0;
    double spirit_whisper = 0.0;
};

struct SimulationSnapshot
{
    int tick = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    int viewed_layer = 0;
    std::vector<VoxelState> voxels;
    RegionMetrics metrics;
    std::vector<std::string> recent_events;
};

class Simulator
{
public:
    Simulator();

    void reset();
    void step();
    void apply_intervention(InterventionType intervention);
    void set_viewed_layer(int layer);
    [[nodiscard]] SimulationSnapshot snapshot() const;

private:
    [[nodiscard]] int index_of(int x, int y, int z) const;
    void update_hydrology();
    void update_voxels();
    void push_event(std::string message);

    int width_ = 14;
    int height_ = 8;
    int depth_ = 5;
    int tick_ = 0;
    int viewed_layer_ = 2;

    std::vector<VoxelState> voxels_;
    RegionMetrics metrics_{};
    std::vector<std::string> recent_events_;
};
} // namespace voxel_infrastructure_004
