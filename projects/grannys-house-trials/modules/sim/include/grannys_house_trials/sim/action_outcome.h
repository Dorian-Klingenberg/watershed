#pragma once

#include "grannys_house_trials/sim/evidence_item.h"

#include <string>
#include <vector>

namespace grannys_house_trials::sim
{
struct ActionOutcome
{
    bool success = false;
    std::vector<std::string> observations;
    std::vector<EvidenceItem> evidence;
};
} // namespace grannys_house_trials::sim
