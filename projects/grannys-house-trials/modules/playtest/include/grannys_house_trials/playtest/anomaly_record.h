#pragma once

#include "grannys_house_trials/util/non_empty_string.h"

#include <string_view>

namespace grannys_house_trials::playtest
{
enum class AnomalyKind
{
    Contradiction,
    SuspectedBug,
};

[[nodiscard]] constexpr std::string_view to_string(AnomalyKind kind) noexcept
{
    switch (kind)
    {
    case AnomalyKind::Contradiction:
        return "contradiction";
    case AnomalyKind::SuspectedBug:
        return "suspected_bug";
    }

    return "unknown_anomaly";
}

struct AnomalyRecord
{
    AnomalyKind kind;
    util::NonEmptyString summary;
    util::NonEmptyString detail;
};
} // namespace grannys_house_trials::playtest
