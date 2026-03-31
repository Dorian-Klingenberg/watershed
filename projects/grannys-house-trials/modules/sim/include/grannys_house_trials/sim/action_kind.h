#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class ActionKind
{
    Look,
    Inspect,
    InspectNeighborhood,
    RouteWater,
    DigChannel,
    PackSoil,
    AdvanceSimulation,
    ResetRound,
    Wait,
};

[[nodiscard]] constexpr std::string_view to_string(ActionKind action) noexcept
{
    switch (action)
    {
    case ActionKind::Look:
        return "look";
    case ActionKind::Inspect:
        return "inspect";
    case ActionKind::InspectNeighborhood:
        return "inspect_neighborhood";
    case ActionKind::RouteWater:
        return "route_water";
    case ActionKind::DigChannel:
        return "dig_channel";
    case ActionKind::PackSoil:
        return "pack_soil";
    case ActionKind::AdvanceSimulation:
        return "advance_simulation";
    case ActionKind::ResetRound:
        return "reset_round";
    case ActionKind::Wait:
        return "wait";
    }

    return "unknown_action";
}
} // namespace grannys_house_trials::sim
