#pragma once

#include <string_view>

namespace grannys_house_trials::playtest
{
enum class RoundResult
{
    Active,
    Success,
    Failure,
    Aborted,
};

[[nodiscard]] constexpr std::string_view round_result_name(RoundResult result) noexcept
{
    switch (result)
    {
    case RoundResult::Active:
        return "active";
    case RoundResult::Success:
        return "success";
    case RoundResult::Failure:
        return "failure";
    case RoundResult::Aborted:
        return "aborted";
    }

    return "active";
}
} // namespace grannys_house_trials::playtest
