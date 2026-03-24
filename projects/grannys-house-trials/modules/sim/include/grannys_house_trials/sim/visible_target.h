#pragma once

#include "grannys_house_trials/sim/target_id.h"
#include "grannys_house_trials/sim/target_kind.h"
#include "grannys_house_trials/sim/target_state_tag.h"

#include <vector>

namespace grannys_house_trials::sim
{
struct VisibleTarget
{
    TargetId id;
    TargetKind kind;
    std::vector<TargetStateTag> states;
};
} // namespace grannys_house_trials::sim
