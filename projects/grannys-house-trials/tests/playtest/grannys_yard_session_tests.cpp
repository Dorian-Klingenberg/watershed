#include "grannys_house_trials/playtest/grannys_yard_session.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::sim::TargetId;
using grannys_house_trials::util::NonEmptyString;
using grannys_house_trials::playtest::GrannysYardSession;
using grannys_house_trials::playtest::TesterRole;

TEST_CASE("Granny's Yard session exposes the drainage round packet", "[playtest][grannys_yard_session]")
{
    GrannysYardSession session;

    const auto packet = session.turn_packet();

    REQUIRE(packet.objective.view() == std::string_view{
        "Deliver enough water to the north bed without soaking the cellar edge or softening the yard path."});
    REQUIRE(packet.role == TesterRole::Builder);
    REQUIRE(packet.legal_actions.size() >= static_cast<std::size_t>(3));
}

TEST_CASE("Granny's Yard session can advance and reset the round", "[playtest][grannys_yard_session]")
{
    GrannysYardSession session;

    REQUIRE(session.run_action("inspect_target", TargetId::DrainMouth, NonEmptyString("Auditor")).success);
    REQUIRE(session.run_action("route_water_source", TargetId::DrainMouth, NonEmptyString("Builder")).success);
    REQUIRE(session.advance_round(TargetId::GardenBedNorth).success);
    REQUIRE(session.reset_round().success);
    REQUIRE_FALSE(session.scenario().state().drain_source_routed);
    REQUIRE_FALSE(session.scenario().state().terrace_channel_dug);
    REQUIRE_FALSE(session.scenario().state().objective_completed);
    REQUIRE_FALSE(session.scenario().state().objective_failed);
}

TEST_CASE("Granny's Yard session suppresses repeated no-op events until the world changes", "[playtest][grannys_yard_session]")
{
    GrannysYardSession session;

    REQUIRE(session.advance_round(TargetId::GardenBedNorth, NonEmptyString("Builder")).success);
    const auto after_first_no_op = session.recent_events().size();
    REQUIRE(after_first_no_op > 0);

    REQUIRE(session.advance_round(TargetId::GardenBedNorth, NonEmptyString("Builder")).success);
    REQUIRE(session.recent_events().size() == after_first_no_op);

    REQUIRE(session.run_action("route_water_source", TargetId::DrainMouth, NonEmptyString("Builder")).success);
    const auto after_state_change = session.recent_events().size();

    REQUIRE(session.advance_round(TargetId::GardenBedNorth, NonEmptyString("Builder")).success);
    REQUIRE(session.recent_events().size() > after_state_change);
}
