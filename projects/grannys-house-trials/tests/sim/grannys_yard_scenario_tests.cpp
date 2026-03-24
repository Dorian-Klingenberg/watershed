#include "grannys_house_trials/sim/grannys_yard_scenario.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using grannys_house_trials::sim::AnchorId;
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

TEST_CASE("Granny's Yard starts with a readable porch state", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;

    REQUIRE(scenario.state().current_anchor == AnchorId::Porch);
    REQUIRE(has_action_id(scenario.legal_actions(), "move_path_edge"));
    REQUIRE(has_action_id(scenario.legal_actions(), "inspect_garden_bed_north"));

    const auto targets = scenario.visible_targets();
    const VisibleTarget *garden_bed = find_target(targets, TargetId::GardenBedNorth);

    REQUIRE(garden_bed != nullptr);
    REQUIRE(has_state(*garden_bed, TargetStateTag::Dry));
}

TEST_CASE("Inspecting the drain mouth reveals the buried dependency", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Systems Auditor");

    REQUIRE(scenario.apply_action(actor, "move_path_edge").success);
    REQUIRE(scenario.apply_action(actor, "move_drain_mouth").success);
    const auto outcome = scenario.apply_action(actor, "inspect_drain_mouth");

    REQUIRE(outcome.success);
    REQUIRE(scenario.state().hidden_cross_link_revealed);
    REQUIRE(scenario.round_log().count(EvidenceType::HiddenDependencyRevealed) == static_cast<std::size_t>(1));
    REQUIRE(outcome.evidence.size() == static_cast<std::size_t>(1));
    REQUIRE(outcome.evidence.front().type == EvidenceType::HiddenDependencyRevealed);
}

TEST_CASE("Clearing the terrace cut solves the bed and damages the yard", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Builder");

    REQUIRE(scenario.apply_action(actor, "move_path_edge").success);
    REQUIRE(scenario.apply_action(actor, "move_terrace_cut").success);
    const auto outcome = scenario.apply_action(actor, "clear_blockage_terrace_cut");

    REQUIRE(outcome.success);
    REQUIRE(scenario.state().garden_bed_north_watered);
    REQUIRE(scenario.state().cellar_edge_saturated);
    REQUIRE(scenario.state().path_edge_softened);
    REQUIRE(scenario.round_log().count(EvidenceType::ObjectiveCompleted) == static_cast<std::size_t>(1));
    REQUIRE(scenario.round_log().count(EvidenceType::CollateralDamage) == static_cast<std::size_t>(1));
    REQUIRE_FALSE(has_action_id(scenario.legal_actions(), "clear_blockage_terrace_cut"));
}

TEST_CASE("Reset restores the initial Granny's Yard state", "[sim][grannys_yard]")
{
    GrannysYardScenario scenario;
    const NonEmptyString actor("Chaos Tester");

    REQUIRE(scenario.apply_action(actor, "move_path_edge").success);
    REQUIRE(scenario.apply_action(actor, "move_terrace_cut").success);
    REQUIRE(scenario.apply_action(actor, "clear_blockage_terrace_cut").success);

    scenario.reset();

    REQUIRE(scenario.state().current_anchor == AnchorId::Porch);
    REQUIRE(scenario.state().terrace_cut_blocked);
    REQUIRE_FALSE(scenario.state().garden_bed_north_watered);
    REQUIRE_FALSE(scenario.state().cellar_edge_saturated);
    REQUIRE_FALSE(scenario.state().path_edge_softened);
    REQUIRE(scenario.round_log().size() == static_cast<std::size_t>(0));
}
