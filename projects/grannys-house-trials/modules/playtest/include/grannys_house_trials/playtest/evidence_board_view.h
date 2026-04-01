#pragma once

#include "grannys_house_trials/playtest/round_result.h"
#include "grannys_house_trials/sim/round_log.h"

#include <cstddef>
#include <string>
#include <vector>

namespace grannys_house_trials::playtest
{
struct EvidenceBoardStat
{
    sim::EvidenceType type;
    std::size_t count = 0;
};

struct EvidenceBoardView
{
    std::vector<EvidenceBoardStat> stats;
    std::vector<std::string> highlights;
    RoundResult round_result = RoundResult::Active;
};

[[nodiscard]] EvidenceBoardView make_evidence_board_view(
    const sim::RoundLog &round_log,
    RoundResult round_result = RoundResult::Active);
} // namespace grannys_house_trials::playtest
