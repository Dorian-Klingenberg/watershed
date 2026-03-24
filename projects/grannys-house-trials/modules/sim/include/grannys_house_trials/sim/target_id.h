#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class TargetId
{
    CellarEdge,
    TerraceCut,
    DrainMouth,
    GardenBedNorth,
    FlatStoneRun,
};

[[nodiscard]] constexpr std::string_view to_string(TargetId target) noexcept
{
    switch (target)
    {
    case TargetId::CellarEdge:
        return "cellar_edge";
    case TargetId::TerraceCut:
        return "terrace_cut";
    case TargetId::DrainMouth:
        return "drain_mouth";
    case TargetId::GardenBedNorth:
        return "garden_bed_north";
    case TargetId::FlatStoneRun:
        return "flat_stone_run";
    }

    return "unknown_target";
}
} // namespace grannys_house_trials::sim
