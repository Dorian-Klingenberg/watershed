#include "grannys_house_trials/sim/grannys_yard_scenario.h"

#include "grannys_house_trials/sim/evidence_type.h"

#include <algorithm>
#include <string>
#include <utility>

namespace grannys_house_trials::sim
{
namespace
{
[[nodiscard]] LegalAction make_move_action(std::string_view id, AnchorId destination)
{
    return LegalAction{
        util::NonEmptyString(std::string(id)),
        ActionKind::Move,
        destination,
        std::nullopt,
    };
}

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

std::vector<TargetStateTag> GrannysYardScenario::target_states(TargetId target) const
{
    switch (target)
    {
    case TargetId::CellarEdge:
        if (state_.cellar_edge_saturated)
        {
            return {
                TargetStateTag::Wet,
                TargetStateTag::Soft,
                TargetStateTag::Unstable,
            };
        }

        return {
            TargetStateTag::Damp,
            TargetStateTag::Stable,
        };
    case TargetId::TerraceCut:
        if (state_.terrace_cut_blocked)
        {
            return {
                TargetStateTag::Damp,
                TargetStateTag::Blocked,
            };
        }

        return {
            TargetStateTag::Wet,
            TargetStateTag::Flowing,
        };
    case TargetId::DrainMouth:
        if (state_.hidden_cross_link_revealed)
        {
            return {
                TargetStateTag::Damp,
                TargetStateTag::Revealed,
            };
        }

        return {TargetStateTag::Damp};
    case TargetId::GardenBedNorth:
        if (state_.garden_bed_north_watered)
        {
            return {TargetStateTag::Wet};
        }

        return {TargetStateTag::Dry};
    case TargetId::FlatStoneRun:
        if (state_.hidden_cross_link_revealed)
        {
            return {
                TargetStateTag::Damp,
                TargetStateTag::Revealed,
            };
        }

        return {TargetStateTag::Stable};
    }

    return {};
}

std::vector<VisibleTarget> GrannysYardScenario::visible_targets() const
{
    switch (state_.current_anchor)
    {
    case AnchorId::Porch:
        return {
            make_visible_target(
                TargetId::GardenBedNorth,
                TargetKind::GardenBed,
                target_states(TargetId::GardenBedNorth)),
            make_visible_target(
                TargetId::CellarEdge,
                TargetKind::GroundPatch,
                target_states(TargetId::CellarEdge)),
        };
    case AnchorId::PathEdge:
        return {
            make_visible_target(
                TargetId::TerraceCut,
                TargetKind::Channel,
                target_states(TargetId::TerraceCut)),
            make_visible_target(
                TargetId::FlatStoneRun,
                TargetKind::StoneRun,
                target_states(TargetId::FlatStoneRun)),
            make_visible_target(
                TargetId::CellarEdge,
                TargetKind::GroundPatch,
                target_states(TargetId::CellarEdge)),
            make_visible_target(
                TargetId::GardenBedNorth,
                TargetKind::GardenBed,
                target_states(TargetId::GardenBedNorth)),
        };
    case AnchorId::TerraceCut:
        return {
            make_visible_target(
                TargetId::TerraceCut,
                TargetKind::Channel,
                target_states(TargetId::TerraceCut)),
            make_visible_target(
                TargetId::GardenBedNorth,
                TargetKind::GardenBed,
                target_states(TargetId::GardenBedNorth)),
        };
    case AnchorId::DrainMouth:
        return {
            make_visible_target(
                TargetId::DrainMouth,
                TargetKind::Drain,
                target_states(TargetId::DrainMouth)),
            make_visible_target(
                TargetId::FlatStoneRun,
                TargetKind::StoneRun,
                target_states(TargetId::FlatStoneRun)),
        };
    case AnchorId::CellarLip:
        return {
            make_visible_target(
                TargetId::CellarEdge,
                TargetKind::GroundPatch,
                target_states(TargetId::CellarEdge)),
            make_visible_target(
                TargetId::FlatStoneRun,
                TargetKind::StoneRun,
                target_states(TargetId::FlatStoneRun)),
        };
    case AnchorId::GardenBedNorth:
        return {
            make_visible_target(
                TargetId::GardenBedNorth,
                TargetKind::GardenBed,
                target_states(TargetId::GardenBedNorth)),
            make_visible_target(
                TargetId::TerraceCut,
                TargetKind::Channel,
                target_states(TargetId::TerraceCut)),
        };
    }

    return {};
}

std::vector<LegalAction> GrannysYardScenario::legal_actions() const
{
    std::vector<LegalAction> actions;
    actions.push_back(make_simple_action("look", ActionKind::Look));
    actions.push_back(make_simple_action("wait", ActionKind::Wait));

    switch (state_.current_anchor)
    {
    case AnchorId::Porch:
        actions.push_back(make_move_action("move_path_edge", AnchorId::PathEdge));
        actions.push_back(make_move_action("move_cellar_lip", AnchorId::CellarLip));
        actions.push_back(make_target_action(
            "inspect_garden_bed_north",
            ActionKind::Inspect,
            TargetId::GardenBedNorth));
        break;
    case AnchorId::PathEdge:
        actions.push_back(make_move_action("move_porch", AnchorId::Porch));
        actions.push_back(make_move_action("move_terrace_cut", AnchorId::TerraceCut));
        actions.push_back(make_move_action("move_drain_mouth", AnchorId::DrainMouth));
        actions.push_back(make_move_action("move_cellar_lip", AnchorId::CellarLip));
        actions.push_back(make_move_action("move_garden_bed_north", AnchorId::GardenBedNorth));
        actions.push_back(make_target_action(
            "inspect_terrace_cut",
            ActionKind::Inspect,
            TargetId::TerraceCut));
        actions.push_back(make_target_action(
            "inspect_flat_stone_run",
            ActionKind::Inspect,
            TargetId::FlatStoneRun));
        actions.push_back(make_target_action(
            "inspect_cellar_edge",
            ActionKind::Inspect,
            TargetId::CellarEdge));
        break;
    case AnchorId::TerraceCut:
        actions.push_back(make_move_action("move_path_edge", AnchorId::PathEdge));
        actions.push_back(make_target_action(
            "inspect_terrace_cut",
            ActionKind::Inspect,
            TargetId::TerraceCut));
        actions.push_back(make_target_action(
            "inspect_garden_bed_north",
            ActionKind::Inspect,
            TargetId::GardenBedNorth));

        if (state_.terrace_cut_blocked)
        {
            actions.push_back(make_target_action(
                "clear_blockage_terrace_cut",
                ActionKind::ClearBlockage,
                TargetId::TerraceCut));
        }

        break;
    case AnchorId::DrainMouth:
        actions.push_back(make_move_action("move_path_edge", AnchorId::PathEdge));
        actions.push_back(make_target_action(
            "inspect_drain_mouth",
            ActionKind::Inspect,
            TargetId::DrainMouth));
        actions.push_back(make_target_action(
            "inspect_flat_stone_run",
            ActionKind::Inspect,
            TargetId::FlatStoneRun));
        break;
    case AnchorId::CellarLip:
        actions.push_back(make_move_action("move_path_edge", AnchorId::PathEdge));
        actions.push_back(make_move_action("move_porch", AnchorId::Porch));
        actions.push_back(make_target_action(
            "inspect_cellar_edge",
            ActionKind::Inspect,
            TargetId::CellarEdge));
        actions.push_back(make_target_action(
            "inspect_flat_stone_run",
            ActionKind::Inspect,
            TargetId::FlatStoneRun));
        break;
    case AnchorId::GardenBedNorth:
        actions.push_back(make_move_action("move_path_edge", AnchorId::PathEdge));
        actions.push_back(make_target_action(
            "inspect_garden_bed_north",
            ActionKind::Inspect,
            TargetId::GardenBedNorth));
        actions.push_back(make_target_action(
            "inspect_terrace_cut",
            ActionKind::Inspect,
            TargetId::TerraceCut));
        break;
    }

    return actions;
}

ActionOutcome GrannysYardScenario::apply_action(
    const util::NonEmptyString &actor,
    std::string_view action_id)
{
    ActionOutcome outcome{};
    outcome.success = false;

    const auto actions = legal_actions();
    const auto it = std::find_if(
        actions.begin(),
        actions.end(),
        [action_id](const LegalAction &action) {
            return action.id.view() == action_id;
        });

    if (it == actions.end())
    {
        outcome.observations.push_back("That action is not currently legal from here.");
        return outcome;
    }

    state_.turn_count += 1;
    const LegalAction &action = *it;
    outcome.success = true;

    switch (action.kind)
    {
    case ActionKind::Look:
        switch (state_.current_anchor)
        {
        case AnchorId::Porch:
            outcome.observations.push_back(
                "From the porch, the north bed looks close enough to save if the water can be coaxed uphill.");
            break;
        case AnchorId::PathEdge:
            outcome.observations.push_back(
                "From the path edge, the terrace cut, flat stones, and cellar lip all feel tied to the same wet problem.");
            break;
        case AnchorId::TerraceCut:
            outcome.observations.push_back(
                state_.terrace_cut_blocked
                    ? "The terrace cut is choked with silt and roots."
                    : "Water threads through the terrace cut toward the north bed.");
            break;
        case AnchorId::DrainMouth:
            outcome.observations.push_back(
                "The drain mouth is older than the house around it and still smells faintly of cold runoff.");
            break;
        case AnchorId::CellarLip:
            outcome.observations.push_back(
                state_.cellar_edge_saturated
                    ? "The cellar lip has gone dark and soft."
                    : "The cellar lip is holding for now, but it already feels damp.");
            break;
        case AnchorId::GardenBedNorth:
            outcome.observations.push_back(
                state_.garden_bed_north_watered
                    ? "The north bed glistens with enough water to keep the plants alive."
                    : "The north bed is still too dry to count as a success.");
            break;
        }
        break;
    case ActionKind::Move:
        state_.current_anchor = *action.destination;
        outcome.observations.push_back(
            "Moved to " + std::string(to_string(state_.current_anchor)) + ".");
        break;
    case ActionKind::Inspect:
        switch (*action.target)
        {
        case TargetId::GardenBedNorth:
            outcome.observations.push_back(
                state_.garden_bed_north_watered
                    ? "The north bed has finally taken water."
                    : "The north bed is dry enough that the roots still look stressed.");
            break;
        case TargetId::CellarEdge:
            outcome.observations.push_back(
                state_.cellar_edge_saturated
                    ? "Water has crept into the cellar edge and softened the ground."
                    : "The cellar edge is damp but not yet failing.");
            break;
        case TargetId::TerraceCut:
            outcome.observations.push_back(
                state_.terrace_cut_blocked
                    ? "The terrace cut is visibly blocked with silt."
                    : "The terrace cut is open and carrying water.");
            break;
        case TargetId::DrainMouth:
            outcome.observations.push_back(
                "Scouring around the drain mouth exposes stonework that does not belong to the house.");

            if (!state_.hidden_cross_link_revealed)
            {
                state_.hidden_cross_link_revealed = true;
                record_evidence(
                    round_log_,
                    outcome,
                    actor,
                    EvidenceType::HiddenDependencyRevealed,
                    "The foundation drain is cross-linked to an older terrace conduit.");
            }

            break;
        case TargetId::FlatStoneRun:
            outcome.observations.push_back(
                state_.hidden_cross_link_revealed
                    ? "The flat stones now read like a cover over older runoff geometry."
                    : "Water can be heard somewhere below the flat stones, but the route is still unclear.");
            break;
        }
        break;
    case ActionKind::ClearBlockage:
        if (*action.target == TargetId::TerraceCut && state_.terrace_cut_blocked)
        {
            state_.terrace_cut_blocked = false;
            state_.garden_bed_north_watered = true;
            state_.cellar_edge_saturated = true;
            state_.path_edge_softened = true;

            outcome.observations.push_back(
                "Water breaks through the terrace cut and reaches the north bed.");
            outcome.observations.push_back(
                "A darker wet line spreads toward the cellar edge while the path begins to soften.");

            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::ObjectiveCompleted,
                "Water reached the north garden bed.");
            record_evidence(
                round_log_,
                outcome,
                actor,
                EvidenceType::CollateralDamage,
                "The cellar edge darkened and the path softened after the cut was cleared.");
        }
        else
        {
            outcome.success = false;
            outcome.observations.push_back("There is no blockage here that can be cleared.");
        }

        break;
    case ActionKind::Wait:
        if (state_.garden_bed_north_watered)
        {
            outcome.observations.push_back(
                "Water keeps working through the yard, and the cellar edge does not look any happier.");
        }
        else
        {
            outcome.observations.push_back("Nothing improves on its own.");
        }
        break;
    }

    return outcome;
}
} // namespace grannys_house_trials::sim
