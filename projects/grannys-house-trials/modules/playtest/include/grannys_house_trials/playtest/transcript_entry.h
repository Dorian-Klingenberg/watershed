#pragma once

#include "grannys_house_trials/playtest/action_choice.h"
#include "grannys_house_trials/playtest/tester_role.h"
#include "grannys_house_trials/sim/evidence_item.h"

#include <string>
#include <vector>

namespace grannys_house_trials::playtest
{
struct TranscriptEntry
{
    TesterRole role;
    ActionChoice choice;
    std::vector<std::string> observations;
    std::vector<sim::EvidenceItem> evidence;
};
} // namespace grannys_house_trials::playtest
