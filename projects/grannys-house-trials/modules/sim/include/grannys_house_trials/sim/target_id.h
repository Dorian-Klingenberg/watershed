#pragma once

#include <optional>
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

[[nodiscard]] inline std::optional<TargetId> from_string(std::string_view text) noexcept
{
    if (text == "cellar_edge")
    {
        return TargetId::CellarEdge;
    }

    if (text == "terrace_cut")
    {
        return TargetId::TerraceCut;
    }

    if (text == "drain_mouth")
    {
        return TargetId::DrainMouth;
    }

    if (text == "garden_bed_north")
    {
        return TargetId::GardenBedNorth;
    }

    if (text == "flat_stone_run")
    {
        return TargetId::FlatStoneRun;
    }

    return std::nullopt;
}
} // namespace grannys_house_trials::sim
