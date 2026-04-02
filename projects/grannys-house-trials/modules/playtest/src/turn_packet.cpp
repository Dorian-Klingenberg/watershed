#include "grannys_house_trials/playtest/turn_packet.h"

#include "grannys_house_trials/sim/target_id.h"

#include <string>
#include <utility>

namespace grannys_house_trials::playtest
{
namespace
{
[[nodiscard]] RecommendedAction make_recommended_action(
    std::string_view action_id,
    std::optional<sim::TargetId> target = std::nullopt)
{
    return RecommendedAction{util::NonEmptyString(std::string(action_id)), target};
}

[[nodiscard]] std::vector<RecommendedAction> recommend_actions(
    const sim::GrannysYardState &state,
    RoundResult round_result)
{
    if (round_result != RoundResult::Active)
    {
        return {make_recommended_action("reset_round")};
    }

    std::vector<RecommendedAction> recommendations;
    if (!state.hidden_cross_link_revealed)
    {
        recommendations.push_back(make_recommended_action("inspect_neighborhood", sim::TargetId::DrainMouth));
    }
    if (!state.flat_stone_packed)
    {
        recommendations.push_back(make_recommended_action("pack_flat_stone_run", sim::TargetId::FlatStoneRun));
    }
    if (!state.cellar_edge_packed)
    {
        recommendations.push_back(make_recommended_action("pack_cellar_edge", sim::TargetId::CellarEdge));
    }
    if (!state.terrace_channel_dug)
    {
        recommendations.push_back(make_recommended_action("dig_shallow_channel", sim::TargetId::TerraceCut));
    }

    if (!state.drain_source_routed)
    {
        recommendations.push_back(make_recommended_action("route_water_source", sim::TargetId::DrainMouth));
    }
    else if (state.objective_completed)
    {
        recommendations.push_back(make_recommended_action("close_water_source", sim::TargetId::DrainMouth));
    }
    else if (!state.objective_failed)
    {
        recommendations.push_back(make_recommended_action("advance_simulation", sim::TargetId::DrainMouth));
    }

    return recommendations;
}
} // namespace

TurnPacket make_turn_packet(
    const sim::GrannysYardScenario &scenario,
    TesterRole role,
    std::optional<sim::TargetId> focused_target,
    std::vector<std::string> recent_events,
    std::vector<std::string> human_notes,
    std::optional<RoundResult> round_result)
{
    const RoundResult effective_round_result = round_result.has_value()
        ? *round_result
        : scenario.state().objective_completed
            ? RoundResult::Success
            : scenario.state().objective_failed ? RoundResult::Failure : RoundResult::Active;

    std::vector<sim::EvidenceItem> recent_evidence;
    const auto &entries = scenario.round_log().entries();
    const std::size_t max_recent_evidence = std::min<std::size_t>(entries.size(), 6);
    recent_evidence.insert(
        recent_evidence.end(),
        entries.end() - static_cast<std::ptrdiff_t>(max_recent_evidence),
        entries.end());

    const auto recommendations = recommend_actions(scenario.state(), effective_round_result);

    return TurnPacket{
        role,
        util::NonEmptyString(std::string(sim::GrannysYardScenario::objective_text())),
        scenario.state().current_anchor,
        focused_target,
        scenario.visible_targets(),
        scenario.legal_actions(focused_target),
        std::move(recent_evidence),
        effective_round_result,
        scenario.state().hidden_cross_link_revealed,
        scenario.state().objective_completed,
        scenario.state().objective_failed,
        std::move(recommendations),
        std::move(recent_events),
        std::move(human_notes),
    };
}
} // namespace grannys_house_trials::playtest
