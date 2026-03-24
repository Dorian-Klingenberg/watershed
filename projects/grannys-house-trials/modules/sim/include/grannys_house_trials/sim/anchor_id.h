#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class AnchorId
{
    Porch,
    PathEdge,
    TerraceCut,
    DrainMouth,
    CellarLip,
    GardenBedNorth,
};

[[nodiscard]] constexpr std::string_view to_string(AnchorId anchor) noexcept
{
    switch (anchor)
    {
    case AnchorId::Porch:
        return "porch";
    case AnchorId::PathEdge:
        return "path_edge";
    case AnchorId::TerraceCut:
        return "terrace_cut";
    case AnchorId::DrainMouth:
        return "drain_mouth";
    case AnchorId::CellarLip:
        return "cellar_lip";
    case AnchorId::GardenBedNorth:
        return "garden_bed_north";
    }

    return "unknown_anchor";
}
} // namespace grannys_house_trials::sim
