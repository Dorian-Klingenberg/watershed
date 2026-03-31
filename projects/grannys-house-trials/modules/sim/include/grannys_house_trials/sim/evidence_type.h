#pragma once

#include <string_view>

namespace grannys_house_trials::sim
{
enum class EvidenceType
{
    ObjectiveCompleted,
    ObjectiveProgress,
    ObjectiveFailed,
    HiddenDependencyRevealed,
    FailureReproduced,
    DiagnosisMade,
    CollateralDamage,
    IneffectiveAction,
    SuccessfulCorrectiveAction,
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
    case EvidenceType::ObjectiveProgress:
        return "objective progress";
    case EvidenceType::ObjectiveFailed:
        return "objective failed";
    case EvidenceType::HiddenDependencyRevealed:
        return "hidden dependency revealed";
    case EvidenceType::FailureReproduced:
        return "failure reproduced";
    case EvidenceType::DiagnosisMade:
        return "diagnosis made";
    case EvidenceType::CollateralDamage:
        return "collateral damage";
    case EvidenceType::IneffectiveAction:
        return "ineffective action";
    case EvidenceType::SuccessfulCorrectiveAction:
        return "successful corrective action";
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
