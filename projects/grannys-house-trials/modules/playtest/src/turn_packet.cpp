#include "grannys_house_trials/playtest/turn_packet.h"

#include <utility>

namespace grannys_house_trials::playtest
{
TurnPacket make_turn_packet(
    const sim::GrannysYardScenario &scenario,
    TesterRole role,
    std::vector<std::string> recent_events)
{
    return TurnPacket{
        role,
        util::NonEmptyString(
            "Deliver enough water to the north bed without soaking the cellar edge or softening the yard path."),
        scenario.state().current_anchor,
        scenario.visible_targets(),
        scenario.legal_actions(),
        std::move(recent_events),
    };
}
} // namespace grannys_house_trials::playtest
