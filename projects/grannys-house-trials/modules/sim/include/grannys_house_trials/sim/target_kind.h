#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class TargetKind
{
    GroundPatch,
    Channel,
    Drain,
    GardenBed,
    StoneRun,
};

[[nodiscard]] constexpr std::string_view to_string(TargetKind kind) noexcept
{
    switch (kind)
    {
    case TargetKind::GroundPatch:
        return "ground_patch";
    case TargetKind::Channel:
        return "channel";
    case TargetKind::Drain:
        return "drain";
    case TargetKind::GardenBed:
        return "garden_bed";
    case TargetKind::StoneRun:
        return "stone_run";
    }

    return "unknown_target_kind";
}
} // namespace grannys_house_trials::sim
