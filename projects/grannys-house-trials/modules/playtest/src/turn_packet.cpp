#include "grannys_house_trials/playtest/turn_packet.h"

#include <utility>

namespace grannys_house_trials::playtest
{
TurnPacket make_turn_packet(
    const sim::GrannysYardScenario &scenario,
    TesterRole role,
    std::optional<sim::TargetId> focused_target,
    std::vector<std::string> recent_events,
    std::vector<std::string> human_notes)
{
    std::vector<sim::EvidenceItem> recent_evidence;
    const auto &entries = scenario.round_log().entries();
    const std::size_t max_recent_evidence = std::min<std::size_t>(entries.size(), 6);
    recent_evidence.insert(
        recent_evidence.end(),
        entries.end() - static_cast<std::ptrdiff_t>(max_recent_evidence),
        entries.end());

    return TurnPacket{
        role,
        util::NonEmptyString(std::string(sim::GrannysYardScenario::objective_text())),
        scenario.state().current_anchor,
        focused_target,
        scenario.visible_targets(),
        scenario.legal_actions(focused_target),
        std::move(recent_evidence),
        scenario.state().hidden_cross_link_revealed,
        scenario.state().objective_completed,
        scenario.state().objective_failed,
        std::move(recent_events),
        std::move(human_notes),
    };
}
} // namespace grannys_house_trials::playtest
