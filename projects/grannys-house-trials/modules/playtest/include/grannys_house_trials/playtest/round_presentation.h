#pragma once

#include "grannys_house_trials/playtest/evidence_board_view.h"
#include "grannys_house_trials/playtest/turn_packet.h"

namespace grannys_house_trials::playtest
{
struct RoundPresentation
{
    TurnPacket packet;
    EvidenceBoardView evidence_board;
    bool gameplay_actions_enabled = true;
};
} // namespace grannys_house_trials::playtest
