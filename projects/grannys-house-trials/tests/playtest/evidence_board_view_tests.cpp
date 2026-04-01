#include "grannys_house_trials/playtest/evidence_board_view.h"

#include "grannys_house_trials/sim/evidence_item.h"
#include "grannys_house_trials/sim/evidence_type.h"
#include "grannys_house_trials/sim/round_log.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using grannys_house_trials::playtest::EvidenceBoardView;
using grannys_house_trials::playtest::RoundResult;
using grannys_house_trials::playtest::make_evidence_board_view;
using grannys_house_trials::sim::EvidenceItem;
using grannys_house_trials::sim::EvidenceType;
using grannys_house_trials::sim::RoundLog;
using grannys_house_trials::util::NonEmptyString;

namespace
{
[[nodiscard]] std::size_t count_for(
    const EvidenceBoardView &view,
    EvidenceType type)
{
    const auto it = std::find_if(
        view.stats.begin(),
        view.stats.end(),
        [type](const auto &stat) {
            return stat.type == type;
        });

    return it == view.stats.end() ? static_cast<std::size_t>(0) : it->count;
}
} // namespace

TEST_CASE("EvidenceBoardView aggregates evidence counts and highlights", "[playtest][evidence_board]")
{
    RoundLog round_log;
    round_log.record(EvidenceItem{
        NonEmptyString("Builder"),
        EvidenceType::ObjectiveCompleted,
        NonEmptyString("Water reached the north garden bed."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Systems Auditor"),
        EvidenceType::HiddenDependencyRevealed,
        NonEmptyString("The foundation drain is cross-linked to an older terrace conduit."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Builder"),
        EvidenceType::ObjectiveCompleted,
        NonEmptyString("The north bed stayed wet after the cut was opened."),
    });

    const EvidenceBoardView view = make_evidence_board_view(round_log);

    REQUIRE(count_for(view, EvidenceType::ObjectiveCompleted) == static_cast<std::size_t>(2));
    REQUIRE(count_for(view, EvidenceType::HiddenDependencyRevealed) == static_cast<std::size_t>(1));
    REQUIRE(view.highlights.size() == static_cast<std::size_t>(3));
    REQUIRE(view.highlights.front() == "Builder: Water reached the north garden bed.");
    REQUIRE(view.round_result == RoundResult::Active);
}
