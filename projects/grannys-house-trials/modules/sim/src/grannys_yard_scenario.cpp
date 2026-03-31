#include "grannys_house_trials/sim/grannys_yard_scenario.h"

#include "grannys_house_trials/sim/evidence_type.h"

#include <algorithm>
#include <string>
#include <utility>

namespace grannys_house_trials::sim
{
namespace
{
[[nodiscard]] LegalAction make_target_action(
    std::string_view id,
    ActionKind kind,
    TargetId target)
{
    return LegalAction{
        util::NonEmptyString(std::string(id)),
        kind,
        std::nullopt,
        target,
    };
}

[[nodiscard]] LegalAction make_simple_action(std::string_view id, ActionKind kind)
{
    return LegalAction{
        util::NonEmptyString(std::string(id)),
        kind,
        std::nullopt,
        std::nullopt,
    };
}

[[nodiscard]] VisibleTarget make_visible_target(
    TargetId id,
    TargetKind kind,
    std::vector<TargetStateTag> states)
{
    return VisibleTarget{id, kind, std::move(states)};
}

void record_evidence(
    RoundLog &round_log,
    ActionOutcome &outcome,
    const util::NonEmptyString &actor,
    EvidenceType type,
    std::string_view description)
{
    EvidenceItem item{
        actor,
        type,
        util::NonEmptyString(std::string(description)),
    };

    round_log.record(item);
    outcome.evidence.push_back(std::move(item));
}

[[nodiscard]] AnchorId anchor_for_target(TargetId target) noexcept
{
    switch (target)
    {
    case TargetId::CellarEdge:
        return AnchorId::CellarLip;
    case TargetId::TerraceCut:
        return AnchorId::TerraceCut;
    case TargetId::DrainMouth:
        return AnchorId::DrainMouth;
    case TargetId::GardenBedNorth:
        return AnchorId::GardenBedNorth;
    case TargetId::FlatStoneRun:
        return AnchorId::PathEdge;
    }

    return AnchorId::Porch;
}
} // namespace

const GrannysYardState &GrannysYardScenario::state() const noexcept
{
    return state_;
}

const RoundLog &GrannysYardScenario::round_log() const noexcept
{
    return round_log_;
}

void GrannysYardScenario::reset()
{
    state_ = GrannysYardState{};
    round_log_ = RoundLog{};
}

void GrannysYardScenario::recompute_outcomes()
{
    state_.garden_bed_north_watered = false;
    state_.cellar_edge_saturated = false;
    state_.path_edge_softened = false;
    state_.objective_completed = false;
    state_.objective_failed = false;

    if (!state_.drain_source_routed)
    {
        return;
    }

    if (!state_.terrace_channel_dug)
    {
        state_.path_edge_softened = !state_.flat_stone_packed;
        return;
    }

    state_.garden_bed_north_watered = true;
    state_.cellar_edge_saturated = state_.hidden_cross_link_active && !state_.cellar_edge_packed;
    state_.path_edge_softened = !state_.flat_stone_packed;

    if (state_.garden_bed_north_watered && !state_.cellar_edge_saturated && !state_.path_edge_softened)
    {
        state_.objective_completed = true;
        return;
    }

    if (state_.garden_bed_north_watered && (state_.cellar_edge_saturated || state_.path_edge_softened))
    {
        state_.objective_failed = true;
    }
}

std::vector<TargetStateTag> GrannysYardScenario::target_states(TargetId target) const
{
    switch (target)
    {
    case TargetId::CellarEdge:
        if (state_.cellar_edge_saturated)
        {
            return {TargetStateTag::Wet, TargetStateTag::Soft, TargetStateTag::Unstable};
        }

        if (state_.cellar_edge_packed)
        {
            return {TargetStateTag::Damp, TargetStateTag::Stable};
        }

        return {TargetStateTag::Damp, TargetStateTag::Stable};
    case TargetId::TerraceCut:
        if (state_.terrace_channel_dug)
        {
            return state_.drain_source_routed
                ? std::vector<TargetStateTag>{TargetStateTag::Wet, TargetStateTag::Flowing}
                : std::vector<TargetStateTag>{TargetStateTag::Damp, TargetStateTag::Stable};
        }

        return {TargetStateTag::Blocked, TargetStateTag::Damp};
    case TargetId::DrainMouth:
        if (state_.hidden_cross_link_revealed)
        {
            return state_.drain_source_routed
                ? std::vector<TargetStateTag>{TargetStateTag::Wet, TargetStateTag::Flowing, TargetStateTag::Revealed}
                : std::vector<TargetStateTag>{TargetStateTag::Damp, TargetStateTag::Revealed};
        }

        return state_.drain_source_routed
            ? std::vector<TargetStateTag>{TargetStateTag::Wet, TargetStateTag::Flowing}
            : std::vector<TargetStateTag>{TargetStateTag::Damp};
    case TargetId::GardenBedNorth:
        return state_.garden_bed_north_watered
            ? std::vector<TargetStateTag>{TargetStateTag::Wet}
            : std::vector<TargetStateTag>{TargetStateTag::Dry};
    case TargetId::FlatStoneRun:
        if (state_.path_edge_softened)
        {
            return state_.hidden_cross_link_revealed
                ? std::vector<TargetStateTag>{TargetStateTag::Wet, TargetStateTag::Soft, TargetStateTag::Revealed}
                : std::vector<TargetStateTag>{TargetStateTag::Wet, TargetStateTag::Soft};
        }

        if (state_.flat_stone_packed)
        {
            return state_.hidden_cross_link_revealed
                ? std::vector<TargetStateTag>{TargetStateTag::Stable, TargetStateTag::Revealed}
                : std::vector<TargetStateTag>{TargetStateTag::Stable};
        }

        return state_.hidden_cross_link_revealed
            ? std::vector<TargetStateTag>{TargetStateTag::Damp, TargetStateTag::Revealed}
            : std::vector<TargetStateTag>{TargetStateTag::Stable};
    }

    return {};
}

std::vector<VisibleTarget> GrannysYardScenario::visible_targets() const
{
    return {
        make_visible_target(TargetId::DrainMouth, TargetKind::Drain, target_states(TargetId::DrainMouth)),
        make_visible_target(TargetId::TerraceCut, TargetKind::Channel, target_states(TargetId::TerraceCut)),
        make_visible_target(TargetId::GardenBedNorth, TargetKind::GardenBed, target_states(TargetId::GardenBedNorth)),
        make_visible_target(TargetId::FlatStoneRun, TargetKind::StoneRun, target_states(TargetId::FlatStoneRun)),
        make_visible_target(TargetId::CellarEdge, TargetKind::GroundPatch, target_states(TargetId::CellarEdge)),
    };
}

std::vector<LegalAction> GrannysYardScenario::legal_actions(std::optional<TargetId> focus) const
{
    std::vector<LegalAction> actions;
    actions.push_back(make_simple_action("look", ActionKind::Look));
    actions.push_back(make_simple_action("advance_simulation", ActionKind::AdvanceSimulation));
    actions.push_back(make_simple_action("reset_round", ActionKind::ResetRound));

    if (!focus.has_value())
    {
        return actions;
    }

    actions.push_back(make_target_action("inspect_target", ActionKind::Inspect, *focus));
    actions.push_back(make_target_action("inspect_neighborhood", ActionKind::InspectNeighborhood, *focus));

    switch (*focus)
    {
    case TargetId::DrainMouth:
        actions.push_back(make_target_action(
            state_.drain_source_routed ? "close_water_source" : "route_water_source",
            ActionKind::RouteWater,
            *focus));
        break;
    case TargetId::TerraceCut:
        if (!state_.terrace_channel_dug)
        {
            actions.push_back(make_target_action("dig_shallow_channel", ActionKind::DigChannel, *focus));
        }
        break;
    case TargetId::CellarEdge:
        if (!state_.cellar_edge_packed)
        {
            actions.push_back(make_target_action("pack_cellar_edge", ActionKind::PackSoil, *focus));
        }
        break;
    case TargetId::FlatStoneRun:
        if (!state_.flat_stone_packed)
        {
            actions.push_back(make_target_action("pack_flat_stone_run", ActionKind::PackSoil, *focus));
        }
        break;
    case TargetId::GardenBedNorth:
        break;
    }

    return actions;
}

ActionOutcome GrannysYardScenario::apply_action(
    const util::NonEmptyString &actor,
    std::string_view action_id,
    std::optional<TargetId> focus)
{
    ActionOutcome outcome{};
    const auto candidate_actions = legal_actions(focus);

    const auto it = std::find_if(
        candidate_actions.begin(),
        candidate_actions.end(),
        [action_id](const LegalAction &action) {
            return action.id.view() == action_id;
        });

    if (it == candidate_actions.end())
    {
        outcome.observations.push_back("That action is not currently legal.");
        return outcome;
    }

    state_.turn_count += 1;
    const LegalAction &action = *it;
    outcome.success = true;

    if (focus.has_value())
    {
        state_.current_anchor = anchor_for_target(*focus);
    }
    else if (action.target.has_value())
    {
        state_.current_anchor = anchor_for_target(*action.target);
    }

    switch (action.kind)
    {
    case ActionKind::Look:
        outcome.observations.push_back(
            "The yard still wants the same thing: get water into the north bed without turning the cellar edge or path into the price.");
        break;
    case ActionKind::Inspect:
        switch (*action.target)
        {
        case TargetId::DrainMouth:
            outcome.observations.push_back(
                "The drain mouth is stone-lined and older than the house built above it.");
            if (!state_.hidden_cross_link_revealed)
            {
                state_.hidden_cross_link_revealed = true;
                record_evidence(
                    round_log_,
                    outcome,
                    actor,
                    EvidenceType::HiddenDependencyRevealed,
                    "Inspecting the drain mouth exposed a buried cross-link into the cellar-side runoff path.");
            }
            break;
        case TargetId::TerraceCut:
            outcome.observations.push_back(
                state_.terrace_channel_dug
                    ? "The terrace cut is open enough to carry flow toward the bed."
                    : "The terrace cut still needs a shallow guiding channel before it will carry water cleanly.");
            break;
        case TargetId::GardenBedNorth:
            outcome.observations.push_back(
                state_.garden_bed_north_watered
                    ? "The north bed is finally holding moisture."
                    : "The north bed is still too dry to count as a win.");
            break;
        case TargetId::FlatStoneRun:
            outcome.observations.push_back(
                state_.hidden_cross_link_revealed
                    ? "The flat stones cover a cross-route that can dump runoff toward the house unless it is packed."
                    : "The flat stones sound hollow under the runoff, but the route below is still ambiguous.");
            break;
        case TargetId::CellarEdge:
            outcome.observations.push_back(
                state_.cellar_edge_saturated
                    ? "The cellar edge has gone dark and soft."
                    : state_.cellar_edge_packed
                        ? "The cellar edge has been packed and should resist a stray wet pulse."
                        : "The cellar edge is vulnerable if the old cross-link starts feeding it.");
            break;
        }
        break;
    case ActionKind::InspectNeighborhood:
        switch (*action.target)
        {
        case TargetId::DrainMouth:
        case TargetId::FlatStoneRun:
            outcome.observations.push_back(
                "Taken together, the drain mouth and stone run suggest the yard still obeys an older drainage geometry than the visible house plan.");
            if (!state_.hidden_cross_link_revealed)
            {
                state_.hidden_cross_link_revealed = true;
                record_evidence(
                    round_log_,
                    outcome,
                    actor,
                    EvidenceType::HiddenDependencyRevealed,
                    "Neighborhood inspection connected the drain mouth to the concealed cellar-side runoff path.");
            }
            break;
        case TargetId::TerraceCut:
            outcome.observations.push_back(
                "The terrace cut, drain mouth, and north bed line up as one water route if someone is willing to shape it.");
            break;
        case TargetId::GardenBedNorth:
            outcome.observations.push_back(
                "The north bed sits high enough that it will need guided flow instead of just loose flooding.");
            break;
        case TargetId::CellarEdge:
            outcome.observations.push_back(
                "The cellar edge lies in the wrong place to tolerate any surprise runoff from the buried cross-link.");
            break;
        }
        break;
    case ActionKind::RouteWater:
        state_.drain_source_routed = !state_.drain_source_routed;
        outcome.observations.push_back(
            state_.drain_source_routed
                ? "The drain mouth has been opened to feed the yard."
                : "The drain mouth has been shut back down.");
        break;
    case ActionKind::DigChannel:
        state_.terrace_channel_dug = true;
        outcome.observations.push_back(
            "A shallow channel now connects the terrace cut toward the north bed.");
        record_evidence(
            round_log_,
            outcome,
            actor,
            EvidenceType::ObjectiveProgress,
            "A guiding channel was dug from the terrace cut toward the north bed.");
        break;
    case ActionKind::PackSoil:
        if (*action.target == TargetId::FlatStoneRun)
        {
            state_.flat_stone_packed = true;
            state_.hidden_cross_link_active = false;
            outcome.observations.push_back(
                "Packing the flat stone run chokes off the buried cross-link before it can leak toward the house.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::SuccessfulCorrectiveAction,
                "Packing the flat stone run shut down the buried cross-link.");
        }
        else if (*action.target == TargetId::CellarEdge)
        {
            state_.cellar_edge_packed = true;
            outcome.observations.push_back(
                "The cellar edge has been packed to better resist stray wetting.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::SuccessfulCorrectiveAction,
                "Packing the cellar edge hardened the most vulnerable ground beside the house.");
        }
        else
        {
            outcome.success = false;
            outcome.observations.push_back("There is no useful packing work to do at that target.");
        }
        break;
    case ActionKind::AdvanceSimulation:
    {
        const bool garden_was_watered = state_.garden_bed_north_watered;
        const bool cellar_was_saturated = state_.cellar_edge_saturated;
        const bool path_was_softened = state_.path_edge_softened;
        const bool objective_was_completed = state_.objective_completed;
        const bool objective_was_failed = state_.objective_failed;

        state_.simulation_step_count += 1;
        recompute_outcomes();

        if (!state_.drain_source_routed)
        {
            outcome.observations.push_back("Without routed water, the yard state does not meaningfully change.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::IneffectiveAction,
                "A simulation step was advanced before any water source was routed.");
            break;
        }

        if (!state_.terrace_channel_dug)
        {
            outcome.observations.push_back(
                "Water wanders out of the source but never reaches the north bed cleanly.");
            if (!path_was_softened && state_.path_edge_softened)
            {
                record_evidence(
                    round_log_,
                    outcome,
                    actor,
                    EvidenceType::FailureReproduced,
                    "Routing water without a guiding terrace channel softened the path instead of watering the bed.");
            }
            else
            {
                record_evidence(
                    round_log_,
                    outcome,
                    actor,
                    EvidenceType::IneffectiveAction,
                    "Water was routed before the terrace channel was dug.");
            }
            break;
        }

        if (!garden_was_watered && state_.garden_bed_north_watered)
        {
            outcome.observations.push_back("Water reaches the north bed.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::ObjectiveProgress,
                "Water reached the north bed during the simulation step.");
        }

        if (!cellar_was_saturated && state_.cellar_edge_saturated)
        {
            outcome.observations.push_back("The cellar edge darkens as the cross-link feeds runoff back toward the house.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::CollateralDamage,
                "Runoff saturated the cellar edge.");
        }

        if (!path_was_softened && state_.path_edge_softened)
        {
            outcome.observations.push_back("The yard path softens under the redirected water.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::CollateralDamage,
                "Runoff softened the yard path.");
        }

        if (!objective_was_completed && state_.objective_completed)
        {
            outcome.observations.push_back(
                "The yard now satisfies the objective: the bed is wet and the collateral damage stayed under control.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::ObjectiveCompleted,
                "The north bed was watered without soaking the cellar edge or softening the path.");
        }
        else if (!objective_was_failed && state_.objective_failed)
        {
            outcome.observations.push_back(
                "The bed takes water, but the yard pays for it elsewhere.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::ObjectiveFailed,
                "Water reached the bed, but collateral damage made the round a failure.");
        }

        if (outcome.observations.empty())
        {
            outcome.observations.push_back("The routed water continues along the current path.");
        }
        break;
    }
    case ActionKind::ResetRound:
        reset();
        outcome.observations.push_back("The yard has been reset to its initial test state.");
        record_evidence(
            round_log_,
            outcome,
            actor,
            EvidenceType::ResetUsed,
            "The round was reset.");
        break;
    case ActionKind::Wait:
        outcome.observations.push_back("Waiting does not help the yard learn anything new.");
        record_evidence(
            round_log_,
            outcome,
            actor,
            EvidenceType::IneffectiveAction,
            "Time was advanced without a useful intervention.");
        break;
    }

    return outcome;
}
} // namespace grannys_house_trials::sim
