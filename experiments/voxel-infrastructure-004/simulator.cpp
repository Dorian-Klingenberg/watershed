#include "simulator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace voxel_infrastructure_004
{
namespace
{
constexpr int event_limit = 10;

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}
} // namespace

Simulator::Simulator()
{
    reset();
}

void Simulator::reset()
{
    tick_ = 0;
    viewed_layer_ = 2;
    const WorldDefinition world = build_world_definition();
    width_ = world.width;
    height_ = world.height;
    depth_ = world.depth;
    voxels_.assign(static_cast<std::size_t>(width_ * height_ * depth_), {});
    metrics_ = RegionMetrics{
        .upland_head = 3.4,
        .leak_flux = 0.58,
        .marsh_depth = 0.62,
        .orchard_supply = 0.34,
        .settlement_stability = 0.56,
        .conduit_integrity = 0.42,
        .collector_clearance = 0.36,
        .spirit_whisper = 0.18,
    };
    recent_events_.clear();
    for (std::size_t index = 0; index < world.voxels.size(); ++index)
    {
        voxels_[index].material = world.voxels[index].material;
        voxels_[index].hidden = world.voxels[index].hidden;
        voxels_[index].exposed = !world.voxels[index].hidden;
    }
    update_voxels();
    push_event("Field note, since you were apparently going to stand here guessing: this terrace is wet from a buried hydraulic failure, not weather. You're welcome.");
}

void Simulator::step()
{
    ++tick_;
    update_hydrology();
    update_voxels();
}

void Simulator::apply_intervention(InterventionType intervention)
{
    switch (intervention)
    {
    case InterventionType::ExcavateInspectionShaft:
        for (int y = 2; y <= 6; ++y)
        {
            VoxelState &voxel = voxels_[static_cast<std::size_t>(index_of(5, y, 3))];
            voxel.material = MaterialKind::InspectionShaft;
            voxel.hidden = false;
            voxel.exposed = true;
        }
        metrics_.settlement_stability = std::max(0.0, metrics_.settlement_stability - 0.06);
        push_event("There. Inspection shaft opened. Exactly where I said the conduit would be, because pattern recognition remains a skill. The ground is settling now, so do try not to look surprised.");
        break;

    case InterventionType::PackTerraceFracture:
        metrics_.conduit_integrity = std::min(1.0, metrics_.conduit_integrity + 0.2);
        metrics_.leak_flux = std::max(0.0, metrics_.leak_flux - 0.18);
        push_event("I've packed the fracture for you. Seepage is dropping already, which is excellent locally and almost certainly inconvenient somewhere downslope, because this system is never allowed to have only one consequence.");
        break;

    case InterventionType::ClearCollectorSpillway:
        metrics_.collector_clearance = std::min(1.0, metrics_.collector_clearance + 0.24);
        metrics_.orchard_supply = std::min(1.0, metrics_.orchard_supply + 0.12);
        push_event("Collector spillway cleared. Astonishingly, water resumed following geometry the moment someone competent removed the blockage. Lower terraces improve; the marsh loses its accidental job as a storage basin.");
        break;

    case InterventionType::VentAncestorWell:
        metrics_.spirit_whisper = std::min(1.0, metrics_.spirit_whisper + 0.34);
        metrics_.upland_head = std::min(5.0, metrics_.upland_head + 0.4);
        push_event("I vented the ancestor well because apparently we are doing this the inadvisable way. Yes, visibility improved. Yes, pressure oscillation worsened. That's what happens when you outsource judgment to something in the stone.");
        break;
    }

    update_voxels();
}

void Simulator::set_viewed_layer(int layer)
{
    viewed_layer_ = std::clamp(layer, 0, depth_ - 1);
}

SimulationSnapshot Simulator::snapshot() const
{
    return SimulationSnapshot{
        .tick = tick_,
        .width = width_,
        .height = height_,
        .depth = depth_,
        .viewed_layer = viewed_layer_,
        .voxels = voxels_,
        .metrics = metrics_,
        .recent_events = recent_events_,
    };
}

int Simulator::index_of(int x, int y, int z) const
{
    return (z * height_ + y) * width_ + x;
}

void Simulator::update_hydrology()
{
    const double pressure_bias = 0.08 * metrics_.spirit_whisper;
    const double leak_drive = clamp01((1.0 - metrics_.conduit_integrity) * 0.9 + pressure_bias);
    const double collector_help = clamp01(metrics_.collector_clearance);

    metrics_.leak_flux = clamp01(metrics_.leak_flux * 0.74 + leak_drive * 0.42);
    metrics_.marsh_depth = clamp01(metrics_.marsh_depth * 0.78 + metrics_.leak_flux * 0.36 - collector_help * 0.1);
    metrics_.orchard_supply = clamp01(metrics_.orchard_supply * 0.72 + collector_help * 0.32 + metrics_.conduit_integrity * 0.12 - metrics_.leak_flux * 0.16);

    const double collapse_risk = 0.28 * metrics_.marsh_depth + 0.26 * (1.0 - metrics_.conduit_integrity) + 0.12 * metrics_.spirit_whisper;
    metrics_.settlement_stability = clamp01(metrics_.settlement_stability * 0.84 + (1.0 - collapse_risk) * 0.18);
    metrics_.upland_head = clamp01((metrics_.upland_head + 0.04 * std::sin(static_cast<double>(tick_) * 0.35)) / 5.0) * 5.0;

    metrics_.conduit_integrity = std::clamp(metrics_.conduit_integrity - 0.002 - 0.01 * metrics_.spirit_whisper, 0.15, 1.0);
    metrics_.collector_clearance = std::clamp(metrics_.collector_clearance - 0.001, 0.18, 1.0);
    metrics_.spirit_whisper = std::clamp(metrics_.spirit_whisper * 0.985, 0.0, 1.0);

    if (metrics_.marsh_depth > 0.82)
    {
        push_event("In case the leaning posts and sweating stones were too subtle, the marsh has reached the settlement edge. You are now in the 'consequences' portion of the lesson.");
    }

    if (metrics_.orchard_supply < 0.22)
    {
        push_event("Downslope update: orchard channels are starving. So congratulations, you've improved the visible problem by creating a quieter one farther away, which is how these ruins punish optimism.");
    }

    if (metrics_.spirit_whisper > 0.5)
    {
        push_event("The voice in the stone is getting positively conversational now. Readability is up, certainly, but if you can't hear the future invoice in that helpful tone, I really can't assist you further.");
    }
}

void Simulator::update_voxels()
{
    for (VoxelState &voxel : voxels_)
    {
        voxel.saturation = 0.0;
        voxel.pressure = 0.0;
        voxel.salinity = 0.0;
        voxel.stress = 0.0;
        if (voxel.material == MaterialKind::AncientConduit ||
            voxel.material == MaterialKind::DelayBasin ||
            voxel.material == MaterialKind::Collector)
        {
            voxel.hidden = !voxel.exposed;
        }
    }

    for (int z = 1; z <= 3; ++z)
    {
        for (int x = 2; x <= 8; ++x)
        {
            VoxelState &surface = voxels_[static_cast<std::size_t>(index_of(x, height_ - 2, z))];
            surface.saturation = clamp01(metrics_.marsh_depth + 0.05 * (3 - z) + 0.04 * (8 - x));
            surface.stress = clamp01(metrics_.marsh_depth * 0.7);
        }
    }

    for (int x = 1; x <= 10; ++x)
    {
        VoxelState &conduit = voxels_[static_cast<std::size_t>(index_of(x, 2, 2))];
        conduit.pressure = clamp01(metrics_.upland_head / 5.0);
        conduit.saturation = 1.0;
        conduit.stress = clamp01((1.0 - metrics_.conduit_integrity) * 0.9);
        conduit.salinity = clamp01(0.12 * metrics_.spirit_whisper);
    }

    for (int z = 1; z <= 3; ++z)
    {
        VoxelState &basin = voxels_[static_cast<std::size_t>(index_of(8, 2, z))];
        basin.pressure = clamp01(metrics_.upland_head / 6.0);
        basin.saturation = clamp01(0.62 + metrics_.leak_flux * 0.28);
        basin.stress = clamp01(metrics_.leak_flux);
    }

    for (int x = 10; x <= 12; ++x)
    {
        VoxelState &collector = voxels_[static_cast<std::size_t>(index_of(x, 3, 2))];
        collector.saturation = clamp01(0.35 + metrics_.orchard_supply * 0.5);
        collector.pressure = clamp01(metrics_.collector_clearance * 0.9);
        collector.stress = clamp01(1.0 - metrics_.collector_clearance);
    }

    for (int y = 2; y <= 4; ++y)
    {
        VoxelState &fracture = voxels_[static_cast<std::size_t>(index_of(6, y, 2))];
        fracture.saturation = clamp01(0.4 + metrics_.leak_flux * 0.55);
        fracture.pressure = clamp01(0.3 + metrics_.upland_head / 8.0);
        fracture.stress = clamp01(0.5 + (1.0 - metrics_.conduit_integrity) * 0.4);
        fracture.exposed = fracture.exposed || metrics_.spirit_whisper > 0.42;
        fracture.hidden = !fracture.exposed;
    }
}

void Simulator::push_event(std::string message)
{
    if (!recent_events_.empty() && recent_events_.front() == message)
    {
        return;
    }

    recent_events_.insert(recent_events_.begin(), std::move(message));
    if (recent_events_.size() > event_limit)
    {
        recent_events_.resize(event_limit);
    }
}
} // namespace voxel_infrastructure_004
