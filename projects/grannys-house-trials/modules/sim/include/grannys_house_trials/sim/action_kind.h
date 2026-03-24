#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class ActionKind
{
    Look,
    Move,
    Inspect,
    ClearBlockage,
    Wait,
};

[[nodiscard]] constexpr std::string_view to_string(ActionKind action) noexcept
{
    switch (action)
    {
    case ActionKind::Look:
        return "look";
    case ActionKind::Move:
        return "move";
    case ActionKind::Inspect:
        return "inspect";
    case ActionKind::ClearBlockage:
        return "clear_blockage";
    case ActionKind::Wait:
        return "wait";
    }

    return "unknown_action";
}
} // namespace grannys_house_trials::sim
