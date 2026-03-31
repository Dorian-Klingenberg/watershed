#include "grannys_house_trials/sim/grannys_yard_scenario.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using grannys_house_trials::sim::EvidenceType;
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

TEST_CASE("Granny's Yard exposes legal actions for a selected target", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;

    const auto drain_actions = scenario.legal_actions(TargetId::DrainMouth);
    const auto terrace_actions = scenario.legal_actions(TargetId::TerraceCut);
    const auto path_actions = scenario.legal_actions(TargetId::FlatStoneRun);

    REQUIRE(has_action_id(drain_actions, "inspect_target"));
    REQUIRE(has_action_id(drain_actions, "inspect_neighborhood"));
    REQUIRE(has_action_id(drain_actions, "route_water_source"));
    REQUIRE(has_action_id(terrace_actions, "dig_shallow_channel"));
    REQUIRE(has_action_id(path_actions, "pack_flat_stone_run"));
    REQUIRE(has_action_id(path_actions, "advance_simulation"));
}

TEST_CASE("Inspecting the drain mouth reveals the hidden dependency", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Systems Auditor");

    const auto outcome = scenario.apply_action(actor, "inspect_target", TargetId::DrainMouth);

    REQUIRE(outcome.success);
    REQUIRE(scenario.state().hidden_cross_link_revealed);
    REQUIRE(scenario.round_log().count(EvidenceType::HiddenDependencyRevealed) == static_cast<std::size_t>(1));
}

TEST_CASE("Routing water without a channel produces an ineffective or failing outcome", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Chaos Tester");

    REQUIRE(scenario.apply_action(actor, "route_water_source", TargetId::DrainMouth).success);
    const auto outcome = scenario.apply_action(actor, "advance_simulation", TargetId::TerraceCut);

    REQUIRE(outcome.success);
    REQUIRE_FALSE(scenario.state().garden_bed_north_watered);
    REQUIRE(scenario.state().path_edge_softened);
    REQUIRE(scenario.round_log().count(EvidenceType::FailureReproduced) == static_cast<std::size_t>(1));
}

TEST_CASE("Digging, routing, and packing can complete the objective safely", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Builder");

    REQUIRE(scenario.apply_action(actor, "inspect_neighborhood", TargetId::DrainMouth).success);
    REQUIRE(scenario.apply_action(actor, "pack_flat_stone_run", TargetId::FlatStoneRun).success);
    REQUIRE(scenario.apply_action(actor, "pack_cellar_edge", TargetId::CellarEdge).success);
    REQUIRE(scenario.apply_action(actor, "dig_shallow_channel", TargetId::TerraceCut).success);
    REQUIRE(scenario.apply_action(actor, "route_water_source", TargetId::DrainMouth).success);

    const auto outcome = scenario.apply_action(actor, "advance_simulation", TargetId::GardenBedNorth);

    REQUIRE(outcome.success);
    REQUIRE(scenario.state().garden_bed_north_watered);
    REQUIRE_FALSE(scenario.state().cellar_edge_saturated);
    REQUIRE_FALSE(scenario.state().path_edge_softened);
    REQUIRE(scenario.state().objective_completed);
    REQUIRE_FALSE(scenario.state().objective_failed);
    REQUIRE(scenario.round_log().count(EvidenceType::ObjectiveCompleted) == static_cast<std::size_t>(1));
}

TEST_CASE("Reset restores the initial Granny's Yard state", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Builder");

    REQUIRE(scenario.apply_action(actor, "pack_flat_stone_run", TargetId::FlatStoneRun).success);
    REQUIRE(scenario.apply_action(actor, "dig_shallow_channel", TargetId::TerraceCut).success);
    REQUIRE(scenario.apply_action(actor, "route_water_source", TargetId::DrainMouth).success);
    REQUIRE(scenario.apply_action(actor, "advance_simulation", TargetId::GardenBedNorth).success);

    scenario.reset();

    REQUIRE_FALSE(scenario.state().drain_source_routed);
    REQUIRE_FALSE(scenario.state().terrace_channel_dug);
    REQUIRE_FALSE(scenario.state().hidden_cross_link_revealed);
    REQUIRE_FALSE(scenario.state().garden_bed_north_watered);
    REQUIRE_FALSE(scenario.state().objective_completed);
    REQUIRE_FALSE(scenario.state().objective_failed);

    REQUIRE(scenario.round_log().size() == static_cast<std::size_t>(0));
}
