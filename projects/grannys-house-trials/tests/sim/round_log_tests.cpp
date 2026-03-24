#include "grannys_house_trials/sim/evidence_item.h"
#include "grannys_house_trials/sim/evidence_type.h"
#include "grannys_house_trials/sim/round_log.h"
#include "grannys_house_trials/util/non_empty_string.h"

#include <catch2/catch_test_macros.hpp>

using grannys_house_trials::sim::EvidenceItem;
using grannys_house_trials::sim::EvidenceType;
using grannys_house_trials::sim::RoundLog;
using grannys_house_trials::util::NonEmptyString;

TEST_CASE("RoundLog preserves evidence order", "[sim][round_log]")
{
    RoundLog round_log;
    round_log.record(EvidenceItem{
        NonEmptyString("Builder"),
        EvidenceType::ObjectiveCompleted,
        NonEmptyString("The tomatoes were watered."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Systems Auditor"),
        EvidenceType::DiagnosisMade,
        NonEmptyString("The drain feeds the cellar edge."),
    });

    REQUIRE(round_log.size() == static_cast<std::size_t>(2));
    REQUIRE(round_log.entries().front().actor.view() == "Builder");
    REQUIRE(round_log.entries().back().type == EvidenceType::DiagnosisMade);
}

TEST_CASE("RoundLog counts evidence by type", "[sim][round_log]")
{
    RoundLog round_log;
    round_log.record(EvidenceItem{
        NonEmptyString("Chaos Tester"),
        EvidenceType::FailureReproduced,
        NonEmptyString("The yard path collapsed again."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Chaos Tester"),
        EvidenceType::FailureReproduced,
        NonEmptyString("The same failure happened with more water."),
    });
    round_log.record(EvidenceItem{
        NonEmptyString("Builder"),
        EvidenceType::ObjectiveCompleted,
        NonEmptyString("One bed received enough water."),
    });

    REQUIRE(round_log.count(EvidenceType::FailureReproduced) == static_cast<std::size_t>(2));
    REQUIRE(round_log.count(EvidenceType::ObjectiveCompleted) == static_cast<std::size_t>(1));
}
