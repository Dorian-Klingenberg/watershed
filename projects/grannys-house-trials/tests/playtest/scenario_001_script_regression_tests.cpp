#include "grannys_house_trials/playtest/grannys_yard_session.h"
#include "grannys_house_trials/playtest/round_result.h"
#include "grannys_house_trials/sim/evidence_type.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using grannys_house_trials::playtest::GrannysYardSession;
using grannys_house_trials::playtest::RoundResult;
using grannys_house_trials::sim::EvidenceType;
using grannys_house_trials::sim::TargetId;
using grannys_house_trials::util::NonEmptyString;

namespace
{
struct ScriptStep
{
    std::string_view action_id;
    TargetId target;
};

template <std::size_t N>
void run_script(
    GrannysYardSession &session,
    const NonEmptyString &actor,
    const std::array<ScriptStep, N> &steps)
{
    for (const auto &step : steps)
    {
        REQUIRE(session.run_action(step.action_id, step.target, actor).success);
    }
}
} // namespace

TEST_CASE("Scenario 001 scripted mitigation-first sequence resolves to success", "[playtest][grannys_yard][scripts]")
{
    GrannysYardSession session;
    const NonEmptyString actor("Richard");
    constexpr std::array<ScriptStep, 6> steps{{
        {"inspect_neighborhood", TargetId::DrainMouth},
        {"pack_flat_stone_run", TargetId::FlatStoneRun},
        {"pack_cellar_edge", TargetId::CellarEdge},
        {"dig_shallow_channel", TargetId::TerraceCut},
        {"route_water_source", TargetId::DrainMouth},
        {"advance_simulation", TargetId::DrainMouth},
    }};

    run_script(session, actor, steps);

    const auto packet = session.turn_packet(TargetId::DrainMouth);
    REQUIRE(packet.round_result == RoundResult::Success);
    REQUIRE(session.scenario().state().objective_completed);
    REQUIRE_FALSE(session.scenario().state().objective_failed);

    const auto &round_log = session.scenario().round_log();
    REQUIRE(round_log.count(EvidenceType::HiddenDependencyRevealed) == static_cast<std::size_t>(1));
    REQUIRE(round_log.count(EvidenceType::SuccessfulCorrectiveAction) == static_cast<std::size_t>(2));
    REQUIRE(round_log.count(EvidenceType::ObjectiveCompleted) == static_cast<std::size_t>(1));
    REQUIRE(round_log.count(EvidenceType::CollateralDamage) == static_cast<std::size_t>(0));
    REQUIRE(round_log.count(EvidenceType::ObjectiveFailed) == static_cast<std::size_t>(0));
}

TEST_CASE("Scenario 001 scripted throughput-first sequence resolves to failure", "[playtest][grannys_yard][scripts]")
{
    GrannysYardSession session;
    const NonEmptyString actor("Jeremy");
    constexpr std::array<ScriptStep, 6> steps{{
        {"route_water_source", TargetId::DrainMouth},
        {"advance_simulation", TargetId::DrainMouth},
        {"inspect_neighborhood", TargetId::DrainMouth},
        {"advance_simulation", TargetId::DrainMouth},
        {"dig_shallow_channel", TargetId::TerraceCut},
        {"advance_simulation", TargetId::DrainMouth},
    }};

    run_script(session, actor, steps);

    const auto packet = session.turn_packet(TargetId::DrainMouth);
    REQUIRE(packet.round_result == RoundResult::Failure);
    REQUIRE_FALSE(session.scenario().state().objective_completed);
    REQUIRE(session.scenario().state().objective_failed);

    const auto &round_log = session.scenario().round_log();
    REQUIRE(round_log.count(EvidenceType::HiddenDependencyRevealed) == static_cast<std::size_t>(1));
    REQUIRE(round_log.count(EvidenceType::ObjectiveFailed) == static_cast<std::size_t>(1));
    REQUIRE(round_log.count(EvidenceType::CollateralDamage) > static_cast<std::size_t>(0));
}