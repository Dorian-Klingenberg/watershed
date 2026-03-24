#pragma once

#include "grannys_house_trials/sim/action_outcome.h"
#include "grannys_house_trials/sim/grannys_yard_state.h"
#include "grannys_house_trials/sim/legal_action.h"
#include "grannys_house_trials/sim/round_log.h"
#include "grannys_house_trials/sim/visible_target.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <string_view>
#include <vector>

namespace grannys_house_trials::sim
{
class GrannysYardScenario
{
public:
    [[nodiscard]] const GrannysYardState &state() const noexcept;
    [[nodiscard]] const RoundLog &round_log() const noexcept;

    void reset();

    [[nodiscard]] std::vector<VisibleTarget> visible_targets() const;
    [[nodiscard]] std::vector<LegalAction> legal_actions() const;
    [[nodiscard]] ActionOutcome apply_action(
        const util::NonEmptyString &actor,
        std::string_view action_id);

private:
    [[nodiscard]] std::vector<TargetStateTag> target_states(TargetId target) const;

    GrannysYardState state_{};
    RoundLog round_log_{};
};
} // namespace grannys_house_trials::sim
