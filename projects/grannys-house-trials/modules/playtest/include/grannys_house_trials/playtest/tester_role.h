#pragma once

#include <string_view>

namespace grannys_house_trials::playtest
{
enum class TesterRole
{
    Builder,
    ChaosTester,
    SystemsAuditor,
};

[[nodiscard]] constexpr std::string_view to_string(TesterRole role) noexcept
{
    switch (role)
    {
    case TesterRole::Builder:
        return "builder";
    case TesterRole::ChaosTester:
        return "chaos_tester";
    case TesterRole::SystemsAuditor:
        return "systems_auditor";
    }

    return "unknown_tester_role";
}
} // namespace grannys_house_trials::playtest
