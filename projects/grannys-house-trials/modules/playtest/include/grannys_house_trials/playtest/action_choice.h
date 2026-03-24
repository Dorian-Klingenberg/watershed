#pragma once

#include "grannys_house_trials/util/non_empty_string.h"

#include <optional>

namespace grannys_house_trials::playtest
{
struct ActionChoice
{
    util::NonEmptyString action_id;
    std::optional<util::NonEmptyString> aside;
};
} // namespace grannys_house_trials::playtest
