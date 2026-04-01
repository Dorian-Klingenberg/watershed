#pragma once

#include "grannys_house_trials/playtest/tester_role.h"
#include "grannys_house_trials/playtest/round_presentation.h"
#include "grannys_house_trials/playtest/round_summary.h"
#include "grannys_house_trials/playtest/turn_packet.h"
#include "grannys_house_trials/sim/grannys_yard_scenario.h"
#include "grannys_house_trials/sim/target_id.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace grannys_house_trials::playtest
{
class GrannysYardSession
{
public:
    [[nodiscard]] const sim::GrannysYardScenario &scenario() const noexcept;
    [[nodiscard]] sim::GrannysYardScenario &scenario() noexcept;

    [[nodiscard]] TesterRole active_tester_role() const noexcept;
    void set_active_tester_role(TesterRole role) noexcept;

    [[nodiscard]] std::vector<sim::LegalAction> legal_actions(
        std::optional<sim::TargetId> focused_target = std::nullopt) const;

    [[nodiscard]] TurnPacket turn_packet(std::optional<sim::TargetId> focused_target = std::nullopt) const;
    [[nodiscard]] EvidenceBoardView evidence_board_view() const;
    [[nodiscard]] bool can_accept_gameplay_actions() const noexcept;
    [[nodiscard]] RoundPresentation round_presentation(
        std::optional<sim::TargetId> focused_target = std::nullopt) const;
    [[nodiscard]] bool has_completed_round_summary() const noexcept;
    [[nodiscard]] const std::optional<RoundSummary> &completed_round_summary() const noexcept;

    [[nodiscard]] sim::ActionOutcome run_action(
        std::string_view action_id,
        std::optional<sim::TargetId> focused_target = std::nullopt,
        util::NonEmptyString actor = util::NonEmptyString("viewport_operator"));
    [[nodiscard]] sim::ActionOutcome reset_round(
        util::NonEmptyString actor = util::NonEmptyString("viewport_operator"));
    [[nodiscard]] sim::ActionOutcome end_round(
        std::optional<sim::TargetId> focused_target = std::nullopt,
        util::NonEmptyString actor = util::NonEmptyString("viewport_operator"));
    [[nodiscard]] sim::ActionOutcome advance_round(
        std::optional<sim::TargetId> focused_target = std::nullopt,
        util::NonEmptyString actor = util::NonEmptyString("viewport_operator"));

    [[nodiscard]] const std::vector<std::string> &recent_events() const noexcept;
    [[nodiscard]] const std::vector<std::string> &human_notes() const noexcept;
    void clear_recent_events() noexcept;

private:
    void clear_completed_round_summary_if_starting_new_round(std::string_view action_id);
    void capture_completed_round_summary(RoundResult result, std::optional<sim::TargetId> focused_target);
    [[nodiscard]] RoundResult current_live_round_result() const noexcept;

    void append_recent_events(
        std::string_view action_id,
        std::optional<sim::TargetId> focused_target,
        const sim::ActionOutcome &outcome,
        bool state_changed);
    [[nodiscard]] std::string no_op_event_signature(
        std::string_view action_id,
        std::optional<sim::TargetId> focused_target,
        const sim::ActionOutcome &outcome) const;

    sim::GrannysYardScenario scenario_{};
    TesterRole active_tester_role_ = TesterRole::Builder;
    std::vector<std::string> recent_events_{};
    std::vector<std::string> human_notes_{};
    std::optional<RoundSummary> completed_round_summary_{};
    std::unordered_set<std::string> no_op_event_signatures_{};
};
} // namespace grannys_house_trials::playtest
