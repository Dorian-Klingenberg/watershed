#pragma once

#include "grannys_house_trials/playtest/tester_role.h"
#include "grannys_house_trials/sim/anchor_id.h"
#include "grannys_house_trials/sim/evidence_item.h"
#include "grannys_house_trials/sim/grannys_yard_scenario.h"
#include "grannys_house_trials/sim/legal_action.h"
#include "grannys_house_trials/sim/target_id.h"
#include "grannys_house_trials/sim/visible_target.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <optional>
#include <string>
#include <vector>

namespace grannys_house_trials::playtest
{
struct TurnPacket
{
    TesterRole role;
    util::NonEmptyString objective;
    sim::AnchorId current_anchor;
    std::optional<sim::TargetId> focused_target;
    std::vector<sim::VisibleTarget> visible_targets;
    std::vector<sim::LegalAction> legal_actions;
    std::vector<sim::EvidenceItem> recent_evidence;
    bool hidden_dependency_revealed = false;
    bool objective_completed = false;
    bool objective_failed = false;
    std::vector<std::string> recent_events;
};

[[nodiscard]] TurnPacket make_turn_packet(
    const sim::GrannysYardScenario &scenario,
    TesterRole role,
    std::optional<sim::TargetId> focused_target = std::nullopt,
    std::vector<std::string> recent_events = {});
} // namespace grannys_house_trials::playtest
