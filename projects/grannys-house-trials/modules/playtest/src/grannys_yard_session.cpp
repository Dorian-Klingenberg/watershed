#include "grannys_house_trials/playtest/grannys_yard_session.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace grannys_house_trials::playtest
{
namespace
{
[[nodiscard]] RoundResult current_round_result(const grannys_house_trials::sim::GrannysYardState &state) noexcept
{
    if (state.objective_completed)
    {
        return RoundResult::Success;
    }

    if (state.objective_failed)
    {
        return RoundResult::Failure;
    }

    return RoundResult::Active;
}

[[nodiscard]] bool meaningful_state_changed(
    const grannys_house_trials::sim::GrannysYardState &before,
    const grannys_house_trials::sim::GrannysYardState &after) noexcept
{
    return before.drain_source_routed != after.drain_source_routed
        || before.terrace_channel_dug != after.terrace_channel_dug
        || before.hidden_cross_link_revealed != after.hidden_cross_link_revealed
        || before.hidden_cross_link_active != after.hidden_cross_link_active
        || before.flat_stone_packed != after.flat_stone_packed
        || before.cellar_edge_packed != after.cellar_edge_packed
        || before.garden_bed_north_watered != after.garden_bed_north_watered
        || before.cellar_edge_saturated != after.cellar_edge_saturated
        || before.path_edge_softened != after.path_edge_softened
        || before.objective_completed != after.objective_completed
        || before.objective_failed != after.objective_failed;
}

[[nodiscard]] std::string to_action_hint(const RecommendedAction &action)
{
    std::string hint = action.action_id.str();
    if (action.target.has_value())
    {
        hint += " @ ";
        hint += sim::to_string(*action.target);
    }

    return hint;
}

[[nodiscard]] std::vector<std::string> to_action_hints(const std::vector<RecommendedAction> &actions)
{
    std::vector<std::string> hints;
    hints.reserve(actions.size());
    for (const auto &action : actions)
    {
        hints.push_back(to_action_hint(action));
    }

    return hints;
}
} // namespace

const sim::GrannysYardScenario &GrannysYardSession::scenario() const noexcept
{
    return scenario_;
}

sim::GrannysYardScenario &GrannysYardSession::scenario() noexcept
{
    return scenario_;
}

TesterRole GrannysYardSession::active_tester_role() const noexcept
{
    return active_tester_role_;
}

void GrannysYardSession::set_active_tester_role(TesterRole role) noexcept
{
    active_tester_role_ = role;
}

bool GrannysYardSession::has_completed_round_summary() const noexcept
{
    return completed_round_summary_.has_value();
}

const std::optional<RoundSummary> &GrannysYardSession::completed_round_summary() const noexcept
{
    return completed_round_summary_;
}

std::vector<sim::LegalAction> GrannysYardSession::legal_actions(std::optional<sim::TargetId> focused_target) const
{
    return scenario_.legal_actions(focused_target);
}

TurnPacket GrannysYardSession::turn_packet(std::optional<sim::TargetId> focused_target) const
{
    if (completed_round_summary_.has_value())
    {
        return completed_round_summary_->packet.value();
    }

    return make_turn_packet(
        scenario_,
        active_tester_role_,
        focused_target,
        recent_events_,
        human_notes_,
        std::nullopt);
}

EvidenceBoardView GrannysYardSession::evidence_board_view() const
{
    const auto packet = turn_packet();

    if (completed_round_summary_.has_value())
    {
        auto view = completed_round_summary_->evidence_board;
        view.recommended_actions = to_action_hints(packet.recommended_actions);
        return view;
    }

    auto view = make_evidence_board_view(scenario_.round_log(), current_live_round_result());
    view.recommended_actions = to_action_hints(packet.recommended_actions);
    return view;
}

bool GrannysYardSession::can_accept_gameplay_actions() const noexcept
{
    return !completed_round_summary_.has_value() || scenario_.state().turn_count == 0;
}

RoundPresentation GrannysYardSession::round_presentation(std::optional<sim::TargetId> focused_target) const
{
    auto packet = turn_packet(focused_target);
    auto board = evidence_board_view();
    board.recommended_actions = to_action_hints(packet.recommended_actions);

    return RoundPresentation{
        .packet = std::move(packet),
        .evidence_board = std::move(board),
        .gameplay_actions_enabled = can_accept_gameplay_actions(),
    };
}

sim::ActionOutcome GrannysYardSession::run_action(
    std::string_view action_id,
    std::optional<sim::TargetId> focused_target,
    util::NonEmptyString actor)
{
    clear_completed_round_summary_if_starting_new_round(action_id);

    if (action_id != "reset_round" && completed_round_summary_.has_value() && scenario_.state().turn_count > 0)
    {
        sim::ActionOutcome outcome;
        outcome.observations.push_back("The round has ended. Reset it before taking more actions.");
        return outcome;
    }

    const auto before_state = scenario_.state();
    const auto outcome = scenario_.apply_action(actor, action_id, focused_target);
    const bool state_changed = meaningful_state_changed(before_state, scenario_.state());
    append_recent_events(action_id, focused_target, outcome, state_changed);

    if (outcome.success)
    {
        const RoundResult live_round_result = current_live_round_result();
        if (live_round_result != RoundResult::Active)
        {
            capture_completed_round_summary(live_round_result, focused_target);
        }
    }

    return outcome;
}

sim::ActionOutcome GrannysYardSession::reset_round(util::NonEmptyString actor)
{
    const RoundResult prior_result = current_live_round_result();
    std::optional<sim::TargetId> focused_target = std::nullopt;

    if (prior_result == RoundResult::Active && scenario_.state().turn_count > 0)
    {
        capture_completed_round_summary(RoundResult::Aborted, focused_target);
    }

    const auto outcome = run_action("reset_round", focused_target, std::move(actor));
    recent_events_.clear();
    human_notes_.clear();

    return outcome;
}

sim::ActionOutcome GrannysYardSession::end_round(
    std::optional<sim::TargetId> focused_target,
    util::NonEmptyString actor)
{
    (void)actor;

    sim::ActionOutcome outcome;

    if (completed_round_summary_.has_value())
    {
        outcome.observations.push_back("The round has already ended.");
        outcome.success = true;
        return outcome;
    }

    capture_completed_round_summary(RoundResult::Aborted, focused_target);
    outcome.observations.push_back("The round has ended. Reset it to start a new attempt.");
    outcome.success = true;
    return outcome;
}

sim::ActionOutcome GrannysYardSession::advance_round(
    std::optional<sim::TargetId> focused_target,
    util::NonEmptyString actor)
{
    return run_action("advance_simulation", focused_target, std::move(actor));
}

const std::vector<std::string> &GrannysYardSession::recent_events() const noexcept
{
    return recent_events_;
}

const std::vector<std::string> &GrannysYardSession::human_notes() const noexcept
{
    return human_notes_;
}

void GrannysYardSession::clear_recent_events() noexcept
{
    recent_events_.clear();
    human_notes_.clear();
}

void GrannysYardSession::clear_completed_round_summary_if_starting_new_round(std::string_view action_id)
{
    if (action_id != "reset_round"
        && completed_round_summary_.has_value()
        && scenario_.state().turn_count == 0)
    {
        completed_round_summary_.reset();
    }
}

void GrannysYardSession::capture_completed_round_summary(
    RoundResult result,
    std::optional<sim::TargetId> focused_target)
{
    RoundSummary summary{};
    summary.result = result;
    summary.packet = make_turn_packet(
        scenario_,
        active_tester_role_,
        focused_target,
        recent_events_,
        human_notes_,
        result);
    summary.evidence_board = make_evidence_board_view(scenario_.round_log(), result);
    summary.evidence_board.recommended_actions = to_action_hints(summary.packet->recommended_actions);
    completed_round_summary_ = std::move(summary);
}

RoundResult GrannysYardSession::current_live_round_result() const noexcept
{
    return current_round_result(scenario_.state());
}

void GrannysYardSession::append_recent_events(
    std::string_view action_id,
    std::optional<sim::TargetId> focused_target,
    const sim::ActionOutcome &outcome,
    bool state_changed)
{
    const auto target_text = focused_target.has_value()
        ? std::string(grannys_house_trials::sim::to_string(*focused_target))
        : std::string("none");
    const auto event_signature = no_op_event_signature(action_id, focused_target, outcome);

    if (!state_changed && no_op_event_signatures_.contains(event_signature))
    {
        return;
    }

    human_notes_.push_back(std::string(action_id) + " -> " + (outcome.success ? "ok" : "fail") + " @ " + target_text);
    if (outcome.observations.empty())
    {
        recent_events_.push_back(
            std::string("evt|action=")
            + std::string(action_id)
            + "|target="
            + target_text
            + "|ok="
            + (outcome.success ? "1" : "0"));
    }
    else
    {
        for (const std::string &observation : outcome.observations)
        {
            recent_events_.push_back(
                std::string("evt|action=")
                + std::string(action_id)
                + "|target="
                + target_text
                + "|ok="
                + (outcome.success ? "1" : "0")
                + "|obs="
                + observation);
        }
    }

    if (state_changed)
    {
        no_op_event_signatures_.clear();
    }
    else
    {
        no_op_event_signatures_.insert(event_signature);
    }

    constexpr std::size_t max_recent_events = 8;
    if (recent_events_.size() > max_recent_events)
    {
        recent_events_.erase(
            recent_events_.begin(),
            recent_events_.begin()
            + static_cast<std::ptrdiff_t>(recent_events_.size() - max_recent_events));
    }
}

std::string GrannysYardSession::no_op_event_signature(
    std::string_view action_id,
    std::optional<sim::TargetId> focused_target,
    const sim::ActionOutcome &) const
{
    std::ostringstream stream;
    stream << action_id << '|';
    if (focused_target.has_value())
    {
        stream << grannys_house_trials::sim::to_string(*focused_target);
    }
    else
    {
        stream << "none";
    }

    return stream.str();
}
} // namespace grannys_house_trials::playtest
