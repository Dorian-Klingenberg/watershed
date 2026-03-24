#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class TargetStateTag
{
    Dry,
    Damp,
    Wet,
    Soft,
    Stable,
    Unstable,
    Flowing,
    Blocked,
    Revealed,
};

[[nodiscard]] constexpr std::string_view to_string(TargetStateTag tag) noexcept
{
    switch (tag)
    {
    case TargetStateTag::Dry:
        return "dry";
    case TargetStateTag::Damp:
        return "damp";
    case TargetStateTag::Wet:
        return "wet";
    case TargetStateTag::Soft:
        return "soft";
    case TargetStateTag::Stable:
        return "stable";
    case TargetStateTag::Unstable:
        return "unstable";
    case TargetStateTag::Flowing:
        return "flowing";
    case TargetStateTag::Blocked:
        return "blocked";
    case TargetStateTag::Revealed:
        return "revealed";
    }

    return "unknown_target_state";
}
} // namespace grannys_house_trials::sim
