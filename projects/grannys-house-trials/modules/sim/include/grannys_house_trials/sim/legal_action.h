#pragma once

#include "grannys_house_trials/sim/action_kind.h"
#include "grannys_house_trials/sim/anchor_id.h"
#include "grannys_house_trials/sim/target_id.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <optional>

namespace grannys_house_trials::sim
{
struct LegalAction
{
    util::NonEmptyString id;
    ActionKind kind;
    std::optional<AnchorId> destination;
    std::optional<TargetId> target;
};
} // namespace grannys_house_trials::sim
