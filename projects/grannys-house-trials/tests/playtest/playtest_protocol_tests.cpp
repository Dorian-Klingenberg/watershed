#include "grannys_house_trials/playtest/turn_packet.h"
#include "grannys_house_trials/playtest/round_result.h"

#include "grannys_house_trials/sim/grannys_yard_scenario.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using grannys_house_trials::playtest::TesterRole;
using grannys_house_trials::playtest::RoundResult;
using grannys_house_trials::playtest::TurnPacket;
using grannys_house_trials::playtest::make_turn_packet;
using grannys_house_trials::sim::AnchorId;
using grannys_house_trials::sim::GrannysYardScenario;
using grannys_house_trials::sim::LegalAction;
using grannys_house_trials::sim::TargetId;
using grannys_house_trials::sim::TargetStateTag;
using grannys_house_trials::sim::VisibleTarget;
using grannys_house_trials::util::NonEmptyString;

namespace
{
[[nodiscard]] bool has_action_id(
    const std::vector<LegalAction> &actions,
    std::string_view action_id)
{
    return std::any_of(
        actions.begin(),
        actions.end(),
        [action_id](const LegalAction &action) {
            return action.id.view() == action_id;
        });
}

[[nodiscard]] const VisibleTarget *find_target(
    const std::vector<VisibleTarget> &targets,
    TargetId id)
{
    const auto it = std::find_if(
        targets.begin(),
        targets.end(),
        [id](const VisibleTarget &target) {
            return target.id == id;
        });

    return it == targets.end() ? nullptr : &(*it);
}

[[nodiscard]] bool has_state(const VisibleTarget &target, TargetStateTag tag)
{
    return std::find(target.states.begin(), target.states.end(), tag) != target.states.end();
}
} // namespace

TEST_CASE("TurnPacket mirrors the current Granny's Yard turn surface", "[playtest][turn_packet]")
{
    GrannysYardScenario scenario;

    const TurnPacket packet = make_turn_packet(
        scenario,
        TesterRole::Builder,
        TargetId::DrainMouth,
        {"Round start: the north bed is still dry."});

    REQUIRE(packet.role == TesterRole::Builder);
    REQUIRE(packet.current_anchor == AnchorId::Porch);
    REQUIRE(packet.focused_target == TargetId::DrainMouth);
    REQUIRE(packet.objective.view() == "Deliver enough water to the north bed without soaking the cellar edge or softening the yard path.");
    REQUIRE(packet.recent_events.size() == static_cast<std::size_t>(1));
    REQUIRE(has_action_id(packet.legal_actions, "route_water_source"));
    REQUIRE_FALSE(packet.recommended_actions.empty());
    REQUIRE(packet.recommended_actions.front().action_id.view() == "inspect_neighborhood");
    REQUIRE(packet.recommended_actions.front().target == TargetId::DrainMouth);
    REQUIRE(packet.round_result == RoundResult::Active);
    REQUIRE_FALSE(packet.objective_completed);
    REQUIRE_FALSE(packet.objective_failed);
}

TEST_CASE("TurnPacket reflects world changes after a risky intervention", "[playtest][turn_packet]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Builder");

    REQUIRE(scenario.apply_action(actor, "dig_shallow_channel", TargetId::TerraceCut).success);
    REQUIRE(scenario.apply_action(actor, "route_water_source", TargetId::DrainMouth).success);
    REQUIRE(scenario.apply_action(actor, "advance_simulation", TargetId::GardenBedNorth).success);

    const TurnPacket packet = make_turn_packet(
        scenario,
        TesterRole::SystemsAuditor,
        TargetId::GardenBedNorth,
        {"Builder cleared the terrace cut."});

    REQUIRE(packet.current_anchor == AnchorId::GardenBedNorth);

    const VisibleTarget *garden_bed = find_target(packet.visible_targets, TargetId::GardenBedNorth);
    const VisibleTarget *cellar_edge = find_target(packet.visible_targets, TargetId::CellarEdge);

    REQUIRE(garden_bed != nullptr);
    REQUIRE(cellar_edge != nullptr);
    REQUIRE(has_state(*garden_bed, TargetStateTag::Wet));
    REQUIRE(has_state(*cellar_edge, TargetStateTag::Wet));
    REQUIRE(packet.objective_failed);
    REQUIRE(packet.round_result == RoundResult::Failure);
    REQUIRE_FALSE(packet.recent_evidence.empty());
    REQUIRE(packet.recommended_actions.size() == static_cast<std::size_t>(1));
    REQUIRE(packet.recommended_actions.front().action_id.view() == "reset_round");
    REQUIRE_FALSE(packet.recommended_actions.front().target.has_value());
}
