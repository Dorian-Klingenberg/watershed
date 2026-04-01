#include "grannys_house_trials/playtest/grannys_yard_session.h"
#include "grannys_house_trials/playtest/round_presentation.h"
#include "grannys_house_trials/playtest/round_result.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::playtest::GrannysYardSession;
using grannys_house_trials::playtest::RoundPresentation;
using grannys_house_trials::playtest::RoundResult;
using grannys_house_trials::util::NonEmptyString;
using grannys_house_trials::sim::TargetId;

TEST_CASE("Round result stays active until the round resolves", "[playtest][round_result]")
{
    GrannysYardSession session;

    REQUIRE(session.turn_packet().round_result == RoundResult::Active);
    REQUIRE(session.can_accept_gameplay_actions());
    const RoundPresentation presentation = session.round_presentation();
    REQUIRE(presentation.packet.round_result == RoundResult::Active);
    REQUIRE(presentation.evidence_board.round_result == RoundResult::Active);
    REQUIRE(presentation.gameplay_actions_enabled);
}

TEST_CASE("Round result becomes success and blocks more gameplay until reset", "[playtest][round_result]")
{
    GrannysYardSession session;

    REQUIRE(session.run_action("inspect_neighborhood", TargetId::DrainMouth, NonEmptyString("Builder")).success);
    REQUIRE(session.run_action("pack_flat_stone_run", TargetId::FlatStoneRun, NonEmptyString("Builder")).success);
    REQUIRE(session.run_action("pack_cellar_edge", TargetId::CellarEdge, NonEmptyString("Builder")).success);
    REQUIRE(session.run_action("dig_shallow_channel", TargetId::TerraceCut, NonEmptyString("Builder")).success);
    REQUIRE(session.run_action("route_water_source", TargetId::DrainMouth, NonEmptyString("Builder")).success);
    REQUIRE(session.run_action("advance_simulation", TargetId::GardenBedNorth, NonEmptyString("Builder")).success);

    REQUIRE(session.turn_packet().round_result == RoundResult::Success);
    REQUIRE_FALSE(session.can_accept_gameplay_actions());
    REQUIRE_FALSE(session.run_action("look", std::nullopt, NonEmptyString("Builder")).success);
}

TEST_CASE("End round captures an aborted summary for the evidence board", "[playtest][round_result]")
{
    GrannysYardSession session;

    REQUIRE(session.run_action("inspect_target", TargetId::DrainMouth, NonEmptyString("Auditor")).success);
    REQUIRE(session.end_round(TargetId::DrainMouth, NonEmptyString("Host")).success);
    REQUIRE(session.turn_packet().round_result == RoundResult::Aborted);
    REQUIRE(session.evidence_board_view().round_result == RoundResult::Aborted);
    REQUIRE_FALSE(session.can_accept_gameplay_actions());
}

TEST_CASE("Reset preserves an aborted summary until the next round starts", "[playtest][round_result]")
{
    GrannysYardSession session;

    REQUIRE(session.run_action("inspect_target", TargetId::DrainMouth, NonEmptyString("Auditor")).success);
    REQUIRE(session.reset_round(NonEmptyString("Host")).success);
    REQUIRE(session.turn_packet().round_result == RoundResult::Aborted);
    REQUIRE(session.can_accept_gameplay_actions());

    REQUIRE(session.run_action("look", std::nullopt, NonEmptyString("Builder")).success);
    REQUIRE(session.turn_packet().round_result == RoundResult::Active);
}
