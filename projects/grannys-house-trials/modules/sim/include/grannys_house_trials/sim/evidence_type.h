#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class EvidenceType
{
    ObjectiveCompleted,
    HiddenDependencyRevealed,
    FailureReproduced,
    DiagnosisMade,
    CollateralDamage,
    ResetUsed,
    PredictionConfirmed,
    PredictionFailed,
    AnomalyObserved,
    ContradictionLogged,
};

[[nodiscard]] constexpr std::string_view to_string(EvidenceType type) noexcept
{
    switch (type)
    {
    case EvidenceType::ObjectiveCompleted:
        return "objective completed";
    case EvidenceType::HiddenDependencyRevealed:
        return "hidden dependency revealed";
    case EvidenceType::FailureReproduced:
        return "failure reproduced";
    case EvidenceType::DiagnosisMade:
        return "diagnosis made";
    case EvidenceType::CollateralDamage:
        return "collateral damage";
    case EvidenceType::ResetUsed:
        return "reset used";
    case EvidenceType::PredictionConfirmed:
        return "prediction confirmed";
    case EvidenceType::PredictionFailed:
        return "prediction failed";
    case EvidenceType::AnomalyObserved:
        return "anomaly observed";
    case EvidenceType::ContradictionLogged:
        return "contradiction logged";
    }

    return "unknown evidence";
}
} // namespace grannys_house_trials::sim
