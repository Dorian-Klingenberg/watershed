#pragma once

#include "grannys_house_trials/sim/evidence_type.h"
#include "grannys_house_trials/util/non_empty_string.h"

namespace grannys_house_trials::sim
{
struct EvidenceItem
{
    util::NonEmptyString actor;
    EvidenceType type;
    util::NonEmptyString description;
};
} // namespace grannys_house_trials::sim

