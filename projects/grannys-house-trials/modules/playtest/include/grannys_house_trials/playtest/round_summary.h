#pragma once

#include "grannys_house_trials/playtest/evidence_board_view.h"
#include "grannys_house_trials/playtest/round_result.h"
#include "grannys_house_trials/playtest/turn_packet.h"

#include <optional>

namespace grannys_house_trials::playtest
{
struct RoundSummary
{
    RoundResult result = RoundResult::Active;
    std::optional<TurnPacket> packet{};
    EvidenceBoardView evidence_board{};
};
} // namespace grannys_house_trials::playtest
